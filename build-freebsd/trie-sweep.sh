#!/bin/sh
# trie-sweep.sh — run chrome-trie-emulator.py over a set of Mach-Os and report
# which ones dyld2's trieWalk would fault on.
#
#   sh build-freebsd/trie-sweep.sh <file> [file...]
#   sh build-freebsd/trie-sweep.sh --extras            # the overlay's *Extras
#                                                     # wrappers, via $DARLING_OVERLAY
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

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EM="${SCRIPT_DIR}/chrome-trie-emulator.py"
MAX_SYMBOLS="${MAX_SYMBOLS:-25}"

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

[ "$#" -gt 0 ] || { echo "usage: trie-sweep.sh <file>... | --extras" >&2; exit 1; }

total=0
failed=0
skipped=0

for f in "$@"; do
	[ -f "${f}" ] || { printf '%-44s MISSING\n' "$(basename "$f")"; skipped=$((skipped + 1)); continue; }
	names="$(llvm-objdump --macho --exports-trie "${f}" 2>/dev/null \
		| awk '/^0x/ {print $2}' | head -"${MAX_SYMBOLS}")"
	if [ -z "${names}" ]; then
		printf '%-44s no export trie readable by llvm-objdump, skipped\n' "$(basename "$f")"
		skipped=$((skipped + 1))
		continue
	fi
	cnt=0
	bad=0
	for s in ${names} "${names%%_*}_sweep_deliberate_miss"; do
		cnt=$((cnt + 1))
		total=$((total + 1))
		out="$(python3 "${EM}" "${f}" "${s}" 2>&1 | tail -1)"
		case "${out}" in
			*FAIL*)
				bad=$((bad + 1))
				failed=$((failed + 1))
				printf 'FAIL   %-38s %s\n' "$(basename "${f}")" "${s}"
				;;
			*PASS*) ;;
			*)
				bad=$((bad + 1))
				failed=$((failed + 1))
				printf 'ERROR  %-38s %s -- %s\n' "$(basename "${f}")" "${s}" "${out}"
				;;
		esac
	done
	printf '%-44s %3d lookup(s), %d failed\n' "$(basename "${f}")" "${cnt}" "${bad}"
done

printf '\n%s: %d lookup(s) over %d file(s), %d failed, %d file(s) skipped\n' \
	"$([ "${failed}" -eq 0 ] && echo PASS || echo FAIL)" \
	"${total}" "$(( $# - skipped ))" "${failed}" "${skipped}"
[ "${failed}" -eq 0 ]
