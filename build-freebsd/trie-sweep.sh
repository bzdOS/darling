#!/bin/sh
# trie-sweep.sh — run chrome-trie-emulator.py over a set of Mach-Os and report
# which ones dyld2's trieWalk would fault on.
#
#   sh build-freebsd/trie-sweep.sh [--deep] <file> [file...]
#   sh build-freebsd/trie-sweep.sh --extras            # the overlay's *Extras
#                                                     # wrappers, via $DARLING_OVERLAY
#   sh build-freebsd/trie-sweep.sh --all-overlay       # EVERY Mach-O in the
#                                                     # overlay, narrow + shallow + deep
#   sh build-freebsd/trie-sweep.sh --all-audit         # EVERY Mach-O, but the
#                                                     # structural BOUNDS audit:
#                                                     # every node, no names
#
# --deep follows re-export chains: for a name whose terminal node carries
# EXPORT_SYMBOL_FLAGS_REEXPORT it resolves the ordinal against the image's
# dependent-library list -- the same list sniffLoadCommands counts, so
# LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB and LC_LOAD_UPWARD_DYLIB
# in load-command order -- resolves the provider under $DARLING_OVERLAY, and
# repeats the lookup there, which is what findShallowExportedSymbol does after
# trieWalk returns. Requires DARLING_OVERLAY.
#
# For each file it lists the exported names with llvm-objdump --exports-trie --
# an independent implementation, deliberately not the walker under test -- and
# then asks the walker for dyld2's verdict on every one of those names, plus a
# deliberate miss, because a lookup that misses is the case that walks furthest.
#
# Reads only. Writes nothing. Exits 1 if any lookup fails.
#
# Why the names come from llvm-objdump: the walker's own structural enumeration
# cannot be trusted to harvest them (it mis-parses the large tries -- see its
# --enumerate warning), whereas its dyld2-faithful symbol path is the part that
# has been cross-checked. Keeping the name source outside the thing under test
# is the whole point.
#
# --all-overlay is the exhaustiveness mode, and it exists because the earlier
# sweeps were a SAMPLE: 24 hand-picked candidates, which proves those 24 are
# clean and says nothing about the ~380 images the loader walks past on the way
# to the fault. It enumerates every file under the overlay roots, keeps the ones
# dyld2 would route through trieWalk at all (see overlay-qualify.py for the
# three filters and why each one means "unreachable", not "uninteresting"), and
# sweeps each of those shallow AND deep. Two things it has to do that a
# hand-picked list never had to:
#
#   * most of the overlay is FAT (x86_64+i386) and the walker takes only a thin
#     MH_MAGIC_64, so the x86_64 slice is extracted to a temp dir first. The
#     overlay itself is never written to; the trap removes the temp dir.
#   * per-file PASS/FAIL and per-file FILTER reasons are the point, so they are
#     printed one per file rather than only in a total.
#
# Roots default to usr/lib, Frameworks and System/Library/Frameworks, which is
# where the images in the dyld load trace live. Override with OVERLAY_ROOTS
# (colon-separated, relative to $DARLING_OVERLAY) to widen or narrow that.

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EM="${SCRIPT_DIR}/chrome-trie-emulator.py"
QUALIFY="${SCRIPT_DIR}/overlay-qualify.py"
MAX_SYMBOLS="${MAX_SYMBOLS:-25}"
DEEP=""
ALL=""
[ "${1:-}" = "--deep" ] && { DEEP="--deep"; shift; }
[ "${1:-}" = "--all-overlay" ] && { ALL="yes"; shift; }
[ "${1:-}" = "--all-audit" ] && { AUDIT="yes"; shift; }
ROOTS="${DARLING_OVERLAY:-}"

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

command -v llvm-objdump >/dev/null 2>&1 || { echo "FATAL: llvm-objdump not in PATH" >&2; exit 1; }
[ -f "${EM}" ] || { echo "FATAL: ${EM} missing" >&2; exit 1; }

if [ "$1" = "--extras" ]; then
	OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
	# The Extras wrappers are the interesting candidates: §9.9 of
	# build-freebsd/PLAN.md has dyld falling on a wrapper's re-export chain,
	# and every one of them is compressed (LC_DYLD_INFO_ONLY) with an export
	# blob and an LC_REEXPORT_DYLIB, which is exactly the shape that makes
	# dyld2 route the lookup through trieWalk at all.
	set -- "${OD}"/usr/lib/*Extras.dylib
fi

total=0
failed=0
skipped=0
files_done=0
files_bad=0
noenum=0

# sweep_file <file> <label> — every exported name plus a deliberate miss, through
# the walker. <label> is printed with the result ("shallow"/"deep") in
# --all-overlay mode and is empty otherwise, so the plain modes print exactly
# what they always did.
sweep_file() {
	_f="$1"
	_label="$2"
	[ -f "${_f}" ] || { printf '%-44s MISSING\n' "$(basename "${_f}")"; skipped=$((skipped + 1)); return 0; }
	names="$(llvm-objdump --macho --exports-trie "${_f}" 2>/dev/null \
		| awk '/^0x/ {print $2}' | head -"${MAX_SYMBOLS}")"
	if [ -z "${names}" ]; then
		# A QUALIFIED file whose export names llvm-objdump cannot read is not
		# a clean skip -- it is a file with a trie too broken to enumerate, and
		# a broken trie is exactly what this sweep exists to find. Skipping it
		# silently would let a culprit pass as clean, so it is counted apart
		# and kept out of the PASS total. (Found by a negative control: patching
		# one uleb at a trie root makes llvm-objdump give up, and the old code
		# then reported PASS with zero lookups on the very file that is broken.)
		# counted on the shallow pass only, so the verdict counts FILES and not
		# file-modes (every file is swept twice)
		if [ -n "${_label}" ] && [ -z "${DEEP}" ]; then
			noenum=$((noenum + 1))
			printf '  UNENUMERABLE %-46s qualified but llvm-objdump read no export names -- NOT swept, NOT counted clean\n' \
				"${_label}"
		else
			printf '%-44s no export trie readable by llvm-objdump, skipped\n' "$(basename "${_f}")"
			skipped=$((skipped + 1))
		fi
		return 0
	fi
	cnt=0
	bad=0
	for s in ${names} "${names%%_*}_sweep_deliberate_miss"; do
		cnt=$((cnt + 1))
		total=$((total + 1))
		# the root is passed as argv[2] so a provider path is resolved against
		# the overlay, the same way dyld2 resolves it under DYLD_ROOT_PATH
		out="$(python3 "${EM}" ${DEEP} "${_f}" "${s}" ${ROOTS} 2>&1 | tail -1)"
		case "${out}" in
			*FAIL*)
				bad=$((bad + 1))
				failed=$((failed + 1))
				printf 'FAIL   %-38s %s\n' "$(basename "${_f}")" "${s}"
				;;
			*PASS*) ;;
			*)
				bad=$((bad + 1))
				failed=$((failed + 1))
				printf 'ERROR  %-38s %s -- %s\n' "$(basename "${_f}")" "${s}" "${out}"
				;;
		esac
	done
	files_done=$((files_done + 1))
	[ "${bad}" -eq 0 ] || files_bad=$((files_bad + 1))
	if [ -n "${_label}" ]; then
		printf '  %-4s %-52s %3d lookup(s), %d failed\n' \
			"$([ "${bad}" -eq 0 ] && echo PASS || echo FAIL)" "${_label}" "${cnt}" "${bad}"
	else
		printf '%-44s %3d lookup(s), %d failed\n' "$(basename "${_f}")" "${cnt}" "${bad}"
	fi
}

# ------------------------------------------------------------------ all-audit
# The structural bounds audit. Where --all-overlay asks "does a symbol lookup
# survive", this asks the stronger question: can ANY read in trieWalk leave the
# trie, at ANY node, for ANY name. It visits every node through child offsets,
# so it is not limited by which names happen to be tried, and it does not use
# the name-driven walk -- that one can only reach the nodes a lookup reaches,
# and its --enumerate sibling is a documented mis-parse of the large tries.
#
# Each file is also CROSS-CHECKED: the audit reconstructs the set of exported
# names its traversal can spell, and that set must equal llvm-objdump's
# --exports-trie list. Two independent parses agreeing is the evidence that the
# traversal is reading the trie correctly; a mismatch means the audit's
# traversal is wrong about that file, and the file is reported as NOT audited
# rather than quietly passed.
if [ -n "${AUDIT}" ]; then
	OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
	[ -f "${QUALIFY}" ] || { echo "FATAL: ${QUALIFY} missing" >&2; exit 1; }
	SLICES="$(mktemp -d)"
	trap 'rm -rf "${SLICES}"' EXIT INT TERM

	ROOTS_REL="${OVERLAY_ROOTS:-usr/lib:Frameworks:System/Library/Frameworks}"
	set -- $(printf '%s' "${ROOTS_REL}" | tr ':' ' ')
	qualified=0
	filtered=0
	audited=0
	afail=0
	amismatch=0
	aunenum=0
	plan="${SLICES}/plan"
	: >"${plan}"
	echo "== narrowing: every Mach-O under ${ROOTS_REL} =="
	qa_args=""
	for r in "$@"; do
		qa_args="${qa_args} --root ${OD}/${r}"
	done
	# shellcheck disable=SC2086
	python3 "${QUALIFY}" --slice-dir "${SLICES}" ${qa_args} >"${plan}.all" 2>"${plan}.err" || {
		echo "FATAL: overlay-qualify.py failed" >&2; cat "${plan}.err" >&2; exit 1; }
	cat "${plan}.err"
	while IFS="$(printf '\t')" read -r tag a b c; do
		case "${tag}" in
			OK)
				qualified=$((qualified + 1))
				printf '%s\t%s\t%s\n' "${a}" "${b}" "${c}" >>"${plan}"
				;;
			SKIP)
				filtered=$((filtered + 1))
				printf '  FILTER %-58s %s\n' "$(basename "${a}")" "${b}"
				;;
		esac
	done <"${plan}.all"

	echo
	echo "== auditing ${qualified} qualified file(s): every node, every read =="
	while IFS="$(printf '\t')" read -r sweep orig detail; do
		rel="${orig#"${OD}"/}"
		rc=0
		out="$(python3 "${EM}" --audit "${sweep}" 2>&1)" || rc=$?
		verdict="$(printf '%s' "${out}" | sed -n 's/^VERDICT: //p')"
		anames="$(printf '%s' "${out}" | sed -n 's/.*reconstructed: \([0-9]*\).*/\1/p')"
		ahash="$(printf '%s' "${out}" | sed -n 's/.*newline-joined): //p')"
		if [ "${rc}" -ne 0 ] || [ -z "${verdict}" ]; then
			printf '  ERROR   %-52s audit did not produce a verdict (rc=%s)\n' "${rel}" "${rc}"
			afail=$((afail + 1))
			continue
		fi
		# independent name set, for the traversal cross-check
		# The audit does the set comparison itself (it can tell a superset from a
		# loss; the shell cannot). llvm-objdump's list is deliberately NOT
		# required to be equal: it omits terminals the audit reaches and dyld2
		# resolves -- libxpc alone spells 112 re-exported __vproc_* names it never
		# lists. What must never happen is the audit LOSING a name, because then
		# it is not visiting the subtree it claims to have audited.
		llvm-objdump --macho --exports-trie "${sweep}" 2>/dev/null \
			| awk '/^0x/ {print $2}' | LC_ALL=C sort -u >"${SLICES}/names.txt"
		AUDIT_COMPARE_NAMES="${SLICES}/names.txt" python3 "${EM}" --audit "${sweep}" \
			>"${SLICES}/audit.out" 2>&1 || true
		out="$(cat "${SLICES}/audit.out")"
		rc=0; [ -n "${out}" ] || rc=1
		grep -q '^VERDICT: FAIL' "${SLICES}/audit.out" && rc=1
		rel_cmp="$(printf '%s' "${out}" | sed -n 's/^compare: //p')"
		audited=$((audited + 1))
		if [ "${rc}" -eq 0 ]; then
			printf '  PASS    %-52s %5s nodes, %s\n' \
				"${rel}" "$(printf '%s' "${out}" | sed -n 's/^nodes audited: \([0-9]*\).*/\1/p')" \
				"${rel_cmp:-no cross-check}"
		else
			printf '  FAIL    %-52s %s\n' "${rel}" "${rel_cmp:-unguarded read(s)}"
			printf '%s\n' "${out}" | sed -n '/^VERDICT: FAIL/,$p' | sed 's/^/      /'
			afail=$((afail + 1))
		fi
	done <"${plan}"

	seen="$(sed -n 's/.*files_seen=\([0-9]*\).*/\1/p' "${plan}.err")"
	notmacho="$(sed -n 's/.*not_macho=\([0-9]*\).*/\1/p' "${plan}.err")"
	echo
	if [ "${afail}" -eq 0 ] && [ "${amismatch}" -eq 0 ]; then
		printf 'no overlay trie can put trieWalk outside the trie: %d/%d qualified, %d audited, every node and every read in bounds\n' \
			"${audited}" "${qualified}" "${audited}"
		printf '  %s file(s) seen under %s; %d Mach-O, %d qualified, %d filtered, %d not Mach-O\n' \
			"${seen:-?}" "${ROOTS_REL}" "$(( ${seen:-0} - ${notmacho:-0} ))" \
			"${qualified}" "${filtered}" "${notmacho:-0}"
	elif [ "${afail}" -eq 0 ]; then
		printf 'audit INCOMPLETE: %d/%d qualified, %d audited, %d traversal MISMATCH -- not a clean bill of health\n' \
			"${audited}" "${qualified}" "${audited}" "${amismatch}"
	else
		printf 'audit FAIL: %d/%d qualified, %d file(s) with an unguarded read\n' \
			"${qualified}" "${qualified}" "${afail}"
	fi
	[ "${afail}" -eq 0 ] && [ "${amismatch}" -eq 0 ]
	exit $?
fi

# ---------------------------------------------------------------- all-overlay
if [ -n "${ALL}" ]; then
	OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
	[ -f "${QUALIFY}" ] || { echo "FATAL: ${QUALIFY} missing" >&2; exit 1; }
	SLICES="$(mktemp -d)"
	# The overlay is read-only; only the extracted slices are removed.
	trap 'rm -rf "${SLICES}"' EXIT INT TERM

	ROOTS_REL="${OVERLAY_ROOTS:-usr/lib:Frameworks:System/Library/Frameworks}"
	set -- $(printf '%s' "${ROOTS_REL}" | tr ':' ' ')
	qualified=0
	filtered=0
	plan="${SLICES}/plan"
	: >"${plan}"
	echo "== narrowing: every Mach-O under ${ROOTS_REL} =="
	qa_args=""
	for r in "$@"; do
		qa_args="${qa_args} --root ${OD}/${r}"
	done
	# shellcheck disable=SC2086
	python3 "${QUALIFY}" --slice-dir "${SLICES}" ${qa_args} >"${plan}.all" 2>"${plan}.err" || {
		echo "FATAL: overlay-qualify.py failed" >&2; cat "${plan}.err" >&2; exit 1; }
	cat "${plan}.err"
	while IFS="$(printf '\t')" read -r tag a b c; do
		case "${tag}" in
			OK)
				qualified=$((qualified + 1))
				printf '%s\t%s\t%s\t%s\n' "${a}" "${b}" "${c}" "OK" >>"${plan}"
				;;
			SKIP)
				filtered=$((filtered + 1))
				printf '  FILTER %-58s %s\n' "$(basename "${a}")" "${b}"
				;;
		esac
	done <"${plan}.all"

	echo
	echo "== sweeping ${qualified} qualified file(s), shallow then deep =="
	while IFS="$(printf '\t')" read -r sweep orig detail _; do
		# Label with the path RELATIVE to the overlay, not the basename: 381
		# files share only 320 distinct basenames (every framework's binary is
		# named after the framework), so a basename would make 61 files
		# ambiguous in a report whose whole job is to be per-file.
		rel="${orig#"${OD}"/}"
		DEEP=""
		sweep_file "${sweep}" "shallow ${rel}"
		DEEP="--deep"
		sweep_file "${sweep}" "deep    ${rel}"
		DEEP=""
	done <"${plan}"

	seen="$(sed -n 's/.*files_seen=\([0-9]*\).*/\1/p' "${plan}.err")"
	notmacho="$(sed -n 's/.*not_macho=\([0-9]*\).*/\1/p' "${plan}.err")"
	echo
	if [ "${failed}" -eq 0 ] && [ "${noenum}" -eq 0 ]; then
		printf 'offline cleanliness over the whole overlay: %d/%d qualified, all PASS\n' \
			"${qualified}" "${qualified}"
	elif [ "${failed}" -eq 0 ]; then
		printf 'offline cleanliness over the whole overlay: %d/%d qualified, every SWEPT file PASS, but %d NOT swept -- NOT a clean bill of health\n' \
			"${qualified}" "${qualified}" "${noenum}"
	else
		printf 'offline cleanliness over the whole overlay: %d/%d qualified, %d lookup(s) FAILED\n' \
			"${qualified}" "${qualified}" "${failed}"
	fi
	printf '  %s file(s) seen under %s; %d Mach-O, %d qualified, %d filtered, %d not Mach-O\n' \
		"${seen:-?}" "${ROOTS_REL}" "$(( ${seen:-0} - ${notmacho:-0} ))" \
		"${qualified}" "${filtered}" "${notmacho:-0}"
	printf '  %d file(s) swept in BOTH shallow and deep (%d file-modes); %d lookup(s) total; %d failed\n' \
		"$(( files_done / 2 ))" "${files_done}" "${total}" "${failed}"
	[ "${failed}" -eq 0 ]
	exit $?
fi

# ------------------------------------------------------------ existing modes
[ "$#" -gt 0 ] || { echo "usage: trie-sweep.sh <file>... | --extras | --all-overlay" >&2; exit 1; }

for f in "$@"; do
	sweep_file "${f}" ""
done

if [ -n "${DEEP}" ]; then
	printf '\n--deep: re-export chains were followed where a terminal node was a\n'
	printf '        re-export; the chain is listed in each per-file output above.\n'
fi
printf '\n%s: %d lookup(s) over %d file(s), %d failed, %d file(s) skipped\n' \
	"$([ "${failed}" -eq 0 ] && echo PASS || echo FAIL)" \
	"${total}" "$(( $# - skipped ))" "${failed}" "${skipped}"
[ "${failed}" -eq 0 ]
