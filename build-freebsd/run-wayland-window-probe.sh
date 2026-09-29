#!/bin/sh
# run-wayland-window-probe.sh — one command for the whole window-probe run.
#
# The guest run is the one step in this milestone that needs root, so it gets
# exactly one shot at a root prompt. This wraps everything around it and
# refuses to spend that shot on something that could have been checked
# without root: sway not running, the seat still empty, the probe binary not
# built, the vendored backend not matching the one installed. Each of those
# fails in a way that looks like a backend bug from the inside.
#
# Usage:
#   sh build-freebsd/run-wayland-window-probe.sh
#   DRY_RUN=1 sh build-freebsd/run-wayland-window-probe.sh
#
# Env:
#   DARLING_BUILD_DIR   build/scratch dir (same as the other build-freebsd
#                       scripts). Also where launch-dynamic is looked for.
#   DARLING_OVERLAY     the overlay, i.e. the guest's DYLD_ROOT_PATH.
#   WAYLAND_DISPLAY     defaults to wayland-1.
#   XDG_RUNTIME_DIR     discovered if unset: a live sway ipc socket is found
#                       and the runtime dir taken from its path.
#   WAIT_SECS           how long to wait for the seat, and for the run.
#                       Defaults to 60.
#   DRY_RUN=1           do every check and print the exact command that would
#                       have been run, but do not run it. Needs no root.
#   TEST_BIN            which guest test binary to run. Defaults to the window
#                       probe; TEST_BUILD_SH names the script that builds it.
#   LOG                 where the run's log goes. Defaults to
#                       ${DARLING_BUILD_DIR}/wayland-window-probe.log.
#   NO_SEAT=1           skip the sway/conjure steps entirely and do not pass
#                       WAYLAND_DISPLAY or XDG_RUNTIME_DIR to the guest. For a
#                       test that never reaches wl_display_connect this is not
#                       a shortcut: requiring a live seat would make the run
#                       depend on something it does not use. Everything else —
#                       the build gate, the vendored-backend hash, the closure
#                       walk, the probe-shape check, the root run — still runs.
#
# NO PRIVATE PATH LITERALS: every path here is either derived from this
# script's own location or comes from the environment, and the sudo line is
# assembled from variables, so nothing machine-specific can end up in it.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"
BD="${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR to your build dir}"
OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

DRY_RUN="${DRY_RUN:-0}"
WAIT_SECS="${WAIT_SECS:-60}"
NO_SEAT="${NO_SEAT:-0}"
WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
TEST_BIN="${TEST_BIN:-wayland-window-create-macho}"
TEST_BUILD_SH="${TEST_BUILD_SH:-build-wayland-window-test.sh}"
CONJURE="${BD}/conjure-wayland-input"
LOG="${LOG:-${BD}/wayland-window-probe.log}"
CONJURE_LOG="${BD}/conjure-wayland-input.log"

# The backend dylib as committed, and the same file as installed in the
# overlay. They must be byte-identical: the vendored copy is the one the probe
# is checked against, and the overlay copy is the one the guest will actually
# dlopen. If they ever diverge, every conclusion drawn from the vendored copy
# is about a binary that is not running.
VENDORED="${SRC}/tests/vendor/wayland-backend/Wayland"
INSTALLED="${OD}/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents/MacOS/Wayland"
# The hash the vendored copy is committed with (tests/vendor/wayland-backend/
# README.md). Checked so a silent edit of the vendored artifact is noticed
# even if the overlay is edited to match.
EXPECT_SHA="a4797cddc449477fe65317548e58d6c18f050e838804596d470b4f9ae69eff2c"

CONJURE_PID=""

# Set to 1 only once the redirect below has created ${LOG} for THIS run. Until
# then the file at that path, if there is one, belongs to an earlier run and
# `die` must not present it as evidence.
LOG_OWNS_LOG=0

# --- reporting -----------------------------------------------------------

npass=0
step_no=0

ok() {
	npass=$((npass + 1))
	printf '  [PASS] %s\n' "$1"
}

note() {
	printf '         %s\n' "$1"
}

# die <step> <what> <where-to-look>
#
# The third argument is the point of this script: on failure, say which step
# died AND which line of the backend's own log tells us why, so the failure is
# diagnosable from the log alone without re-running anything.
#
# ${LOG} is only tailed when THIS run produced it. A log left behind by an
# earlier run sitting in the same place is worse than no log at all: it is
# plausible, it is large, and it ends at the previous run's failure — so a
# preflight check that has nothing to do with it gets "diagnosed" with it. The
# flag is set only after the redirect below has actually created the file.
die() {
	step_no="$1"
	what="$2"
	look="$3"
	printf '\nFAILED at step %s: %s\n' "${step_no}" "${what}" >&2
	if [ -n "${look}" ]; then
		printf 'look in %s for:\n' "${LOG}" >&2
		printf '%s\n' "${look}" | sed 's/^/    /' >&2
	fi
	if [ "${LOG_OWNS_LOG}" = "1" ] && [ -f "${LOG}" ]; then
	printf '\nlast 20 lines of %s:\n' "${LOG}" >&2
	tail -20 "${LOG}" | sed 's/^/    /' >&2
	printf '\nfull log (loader trace + mldr handler output, interleaved):\n    %s\n' "${LOG}" >&2
	printf 'size: %s bytes, %s lines\n' \
		"$(wc -c <"${LOG}" | tr -d ' ')" "$(wc -l <"${LOG}" | tr -d ' ')" >&2
	if ! grep -q 'trieWalk() malformed trie node' "${LOG}" 2>/dev/null; then
		printf '\nnote: the loader never printed its own bounds complaint\n' >&2
		printf '      "trieWalk() malformed trie node, terminalSize=... extends past\n' >&2
		printf '      end of trie". That line is NOT commented out in dyld2'"'"'s source,\n' >&2
		printf '      so its absence means the fault was NOT at that guard, and is one\n' >&2
		printf '      of the three unchecked reads instead: 1837 (terminalSize = *p++),\n' >&2
		printf '      1862 (edge scan c = *p) or 1876 (child uleb128 skip).\n' >&2
		printf '      The backtrace above places it; the last "dyld: loaded:" line\n' >&2
		printf '      before it names the image.\n' >&2
	fi
elif [ -f "${LOG}" ]; then
	printf '\nno log from this run: %s exists but was not written by it.\n' \
		"${LOG}" >&2
	printf 'it is left over from an earlier run and is not evidence about this one.\n' >&2
fi
	exit 1
}

cleanup() {
	if [ -n "${CONJURE_PID}" ] && kill -0 "${CONJURE_PID}" 2>/dev/null; then
		# The virtual input devices exist only while this process does; sway
		# goes back to an empty seat the moment it goes away. Leaving it
		# behind would make the NEXT run look like it worked.
		kill "${CONJURE_PID}" 2>/dev/null || true
		wait "${CONJURE_PID}" 2>/dev/null || true
		printf '\nstopped conjure-wayland-input (pid %s)\n' "${CONJURE_PID}"
	fi
}
trap cleanup EXIT INT TERM

printf '=== wayland window probe: preflight ===\n'
if [ "${DRY_RUN}" = "1" ]; then
	printf 'DRY_RUN=1 -- every check below runs, the sudo line is only printed\n'
fi

# --- 1. sway is alive and reachable --------------------------------------

step_no=1
if [ "${NO_SEAT}" = "1" ]; then
	note "NO_SEAT=1 -- steps 1, 2 and the seat wait are skipped"
	printf '         (this test never reaches wl_display_connect)\n'
	SWAYSOCK=""
	WAYLAND_SOCK=""
else
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
	# Derive it: the runtime dir is the parent of a live sway ipc socket.
	for sock in /tmp/wayland-*/sway-ipc.*.sock /run/user/*/sway-ipc.*.sock; do
		[ -S "${sock}" ] || continue
		# sway-ipc.<uid>.<pid>.sock -- strip the directory, then ".sock",
		# and only then the last dot field, which is the pid. (Taking the
		# last dot field off the whole path yields "sock".)
		base="${sock##*/}"; base="${base%.sock}"; sway_pid="${base##*.}"
		case "${base}" in
			sway-ipc.*."${sway_pid}") ;;
			*) continue ;;
		esac
		if kill -0 "${sway_pid}" 2>/dev/null; then
			XDG_RUNTIME_DIR="$(dirname "${sock}")"
			SWAYSOCK="${sock}"
			break
		fi
	done
fi
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
	die "${step_no}" "no live sway ipc socket found; is sway running?" ""
fi
if [ -z "${SWAYSOCK:-}" ]; then
	SWAYSOCK=""
	for sock in "${XDG_RUNTIME_DIR}"/sway-ipc.*.sock; do
		[ -S "${sock}" ] || continue
		base="${sock##*/}"; base="${base%.sock}"; sway_pid="${base##*.}"
		case "${base}" in
			sway-ipc.*."${sway_pid}") ;;
			*) continue ;;
		esac
		if kill -0 "${sway_pid}" 2>/dev/null; then
			SWAYSOCK="${sock}"
			break
		fi
	done
fi

WAYLAND_SOCK="${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}"
[ -S "${WAYLAND_SOCK}" ] || die "${step_no}" \
	"no wayland socket at ${WAYLAND_SOCK}" \
	"WaylandDisplay: wl_display_connect failed (WAYLAND_DISPLAY=%s)"
[ -n "${SWAYSOCK}" ] || die "${step_no}" "found a wayland socket but no sway ipc socket" ""
command -v swaymsg >/dev/null 2>&1 || die "${step_no}" "swaymsg not in PATH" ""
ok "sway is alive: ${WAYLAND_SOCK}, ipc ${SWAYSOCK}"
fi # NO_SEAT

# --- 2. conjure-wayland-input is built ------------------------------------

step_no=2
if [ "${NO_SEAT}" = "1" ]; then
	note "NO_SEAT=1 -- conjure-wayland-input is not needed"
elif [ ! -x "${CONJURE}" ]; then
	note "building conjure-wayland-input (it makes the seat non-empty)"
	GEN="${BD}/wlr-protocols-gen"
	mkdir -p "${GEN}"
	# The output names are spelled out rather than looped: conjure-wayland-input.c
	# includes wlr-virtual-pointer-client-protocol.h and
	# virtual-keyboard-client-protocol.h, which are NOT the names wayland-scanner
	# would pick from the xml filenames.
	wayland-scanner client-header \
		"${SCRIPT_DIR}/wlr-protocols/wlr-virtual-pointer-unstable-v1.xml" \
		"${GEN}/wlr-virtual-pointer-client-protocol.h"
	wayland-scanner private-code \
		"${SCRIPT_DIR}/wlr-protocols/wlr-virtual-pointer-unstable-v1.xml" \
		"${GEN}/wlr-virtual-pointer-protocol.c"
	wayland-scanner client-header \
		"${SCRIPT_DIR}/wlr-protocols/virtual-keyboard-unstable-v1.xml" \
		"${GEN}/virtual-keyboard-client-protocol.h"
	wayland-scanner private-code \
		"${SCRIPT_DIR}/wlr-protocols/virtual-keyboard-unstable-v1.xml" \
		"${GEN}/virtual-keyboard-protocol.c"
	# shellcheck disable=SC2086
	clang -O1 -Wall -I"${GEN}" -o "${CONJURE}" \
		"${SCRIPT_DIR}/conjure-wayland-input.c" \
		"${GEN}/wlr-virtual-pointer-protocol.c" \
		"${GEN}/virtual-keyboard-protocol.c" \
		$(pkg-config --cflags --libs wayland-client)
fi
[ -x "${CONJURE}" ] || die "${step_no}" "conjure-wayland-input is not built" ""
ok "conjure-wayland-input: ${CONJURE}"

# --- 3. the probe binary is built ----------------------------------------

step_no=3
if [ ! -f "${SRC}/tests/${TEST_BIN}" ]; then
	note "building the probe (${TEST_BIN})"
	DARLING_SRC_DIR="${SRC}" DARLING_BUILD_DIR="${BD}" DARLING_OVERLAY="${OD}" \
		sh "${SCRIPT_DIR}/${TEST_BUILD_SH}" >"${BD}/build-probe.log" 2>&1 \
		|| die "${step_no}" "the probe failed to build; see ${BD}/build-probe.log" ""
fi
[ -f "${SRC}/tests/${TEST_BIN}" ] || die "${step_no}" \
	"${TEST_BIN} missing after build" ""
ok "probe binary: ${SRC}/tests/${TEST_BIN}"

# The harness is rebuilt whenever it is OLDER THAN ITS SOURCE, not only when it
# is missing. The old condition (missing, or built on demand) meant a change to
# launch-dynamic-smoke.c was silently not exercised: the binary in the build dir
# was from 17 September, eleven days and two staging fixes behind, and a root
# run was spent watching it fail with the pre-fix message and no new
# information. A preflight whose whole job is to make a run's outcome mean
# something cannot be the thing that lets a stale binary decide it.
step_no=3
HARNESS="${BD}/launch-dynamic"
HARNESS_SRC="${SRC}/tests/launch-dynamic-smoke.c"
if [ ! -x "${HARNESS}" ] || [ "${HARNESS_SRC}" -nt "${HARNESS}" ]; then
	note "building launch-dynamic (${HARNESS})"
	# Same recipe as build-all.sh, which is what puts the harness in place.
	cc -o "${HARNESS}" "${HARNESS_SRC}" -lpthread >"${BD}/build-harness.log" 2>&1 \
		|| die "${step_no}" "launch-dynamic failed to build; see ${BD}/build-harness.log" ""
	ok "launch-dynamic rebuilt from ${HARNESS_SRC}"
else
	ok "launch-dynamic: ${HARNESS} (newer than its source)"
fi

# --- 3b. the transitive dylib closure must exist, and must be staged --------
#
# This used to check the probe's 8 DIRECT LC_LOAD_DYLIB entries, and it passed
# 6/6 on a run that then died on a missing Onyx2D: Onyx2D is a dependency of
# AppKit, one level down, so every direct dep was present and the check was
# right about all eight and blind to the one that mattered. A first-ring check
# cannot catch a second-ring miss, so this now walks the whole closure.
#
# Re-export chains are included: LC_REEXPORT_DYLIB is one of the commands
# enumerated, so an image reachable only through a re-export is walked too.
# The compat-version rule the first ring applied to those 8 is now applied to
# every edge of the closure (167 of them for this probe), so nothing the old
# check caught is lost by widening it.
#
# What is FATAL here, and what is not, and why:
#
#   fatal  the overlay is missing something the closure needs, or a name will
#          not resolve, or a version is too old, or the derived list does not
#          cover the closure. No staging list can supply a file the overlay
#          does not have, so no run can work.
#   note   an existing staging cache is short. The harness rebuilds the cache
#          from the derived list during the run below, so failing here would
#          block a run that was about to fix itself, and would leave the
#          machine unable to run the probe until someone hand-deleted a
#          directory.
#
# The check that returns non-zero on a short cache -- the one with the Onyx2D
# verdict -- is check-guest-dylib-compat.py --staging-root, and the controls in
# this file's commit message run exactly that. Preflight is not where a stale
# cache should be fatal; it is where the list that fixes it is derived, and the
# coverage self-check above runs with no cache at all.
step_no=3
CLOSURE_LOG="${BD}/dylib-closure.log"
STAGING_TREES="${BD}/staging-trees.txt"
LOCAL_CACHE="/tmp/darling-local-overlay"

closure_args="${SCRIPT_DIR}/check-guest-dylib-compat.py ${SRC}/tests/${TEST_BIN} ${OD} --closure --emit-staging-trees"
if [ -d "${LOCAL_CACHE}" ]; then
	closure_args="${closure_args} --staging-root ${LOCAL_CACHE}"
	printf 'note: a staging cache exists at %s and is checked below\n' "${LOCAL_CACHE}" >&2
	printf '      it is rebuilt by this run, so a short cache is reported, not fatal\n' >&2
else
	printf 'note: no staging cache yet at %s -- one is built by this run\n' \
		"${LOCAL_CACHE}" >&2
fi

if python3 ${closure_args} >"${CLOSURE_LOG}" 2>&1; then
	n_images="$(sed -n 's/^closure: *\([0-9]*\) image.*/\1/p' "${CLOSURE_LOG}")"
	ok "dylib closure walks clean: ${n_images} image(s), no unresolved names"
else
	# One exit status covers several verdicts, so read the log to find out which
	# one this is. IN-STAGING alone is recoverable -- the run rebuilds the
	# cache. Anything else names a defect no staging list can paper over.
	if grep -q '^IN-STAGING' "${CLOSURE_LOG}" \
			&& ! grep -qE '^(MISSING|UNRESOLVED|OLD|NOID|UNCOVERED)' "${CLOSURE_LOG}"; then
		sed -n '/^IN-STAGING/,/^$/p' "${CLOSURE_LOG}" | sed 's/^/      /' >&2
		n_images="$(sed -n 's/^closure: *\([0-9]*\) image.*/\1/p' "${CLOSURE_LOG}")"
		note "staging cache is short (${n_images} image(s) in the closure); the derived list below covers it"
	else
		die "${step_no}" "the transitive dylib closure is not satisfied (see below)" \
			"dyld: Library not loaded: /System/Library/PrivateFrameworks/Onyx2D...
      Referenced from: /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit
      Reason: image not found"
	fi
fi

# Derive the staging list from the same walk that just passed, and hand it to
# the harness. Deriving it here rather than hardcoding it in the harness is the
# point: the miss that cost the root run was a directory nobody had a reason to
# think about, and a list nobody derives cannot notice one.
# [[:space:]] rather than \t: the escape is a GNU extension, and a preflight
# that silently extracted nothing on another sed would leave the harness on its
# fallback list -- the exact failure this whole change exists to remove.
sed -n 's/^#TREE[[:space:]]*//p' "${CLOSURE_LOG}" >"${STAGING_TREES}"
n_trees="$(wc -l <"${STAGING_TREES}" | tr -d ' ')"
[ "${n_trees}" -gt 0 ] || die "${step_no}" \
	"the closure produced an empty staging list" ""
STAGING_LIST="$(paste -sd: "${STAGING_TREES}")"
printf 'staging list derived from the closure (%d tree(s)):\n' "${n_trees}"
sed 's/^/    /' "${STAGING_TREES}"
ok "staging list derived: ${STAGING_LIST}"


# --- 3c. the probe's Mach-O shape ----------------------------------------
#
# Compared against guest binaries that demonstrably work. Two of these are
# load-time requirements, one is a false alarm worth keeping in mind:
#   * no @rpath -- the guest resolves everything through DYLD_ROOT_PATH (the
#     overlay), so every LC_LOAD_DYLIB must be an absolute guest path (3b
#     enforces that too).
#   * LC_MAIN and x86_64 EXECUTE: what the other guest binaries use.
#   * the 65535.255.255 requirement above is NOT silently ignored because of a
#     header flag: MachOFile::enforceCompatVersion() (MachOFile.cpp:931) has no
#     flag test at all, it returns false only for a deployment target of
#     macOS >= 10.14. This probe targets 10.12, so the requirement really is
#     enforced -- which is why 3b checks the versions instead.
step_no=3
if command -v llvm-objdump >/dev/null 2>&1; then
	# `|| true` on purpose: a command substitution takes the status of the
	# command, so under `set -e` a malformed or unreadable binary would
	# abort the script with llvm-objdump's status and no message at all --
	# the silent confusing failure this whole script exists to prevent.
	# An empty result is caught by the checks below, which say what is wrong.
	probe_hdr="$(llvm-objdump --macho --private-headers "${SRC}/tests/${TEST_BIN}" 2>/dev/null || true)"
	case "${probe_hdr}" in
		*X86_64*EXECUTE*) ;;
		*) die "${step_no}" "the probe is not a Mach-O x86_64 EXECUTE" \
			"dyld: image not found" ;;
	esac
	case "${probe_hdr}" in
		*LC_MAIN*) ;;
		*) die "${step_no}" "the probe has no LC_MAIN entry point" \
			"dyld: no LC_MAIN, cannot start the guest program" ;;
	esac
	if printf '%s' "${probe_hdr}" | grep -q '@rpath'; then
		die "${step_no}" "the probe has an @rpath load command" \
			"dyld: library not loaded: ... @rpath/..."
	fi
	ok "probe shape: x86_64 EXECUTE, LC_MAIN, no @rpath"
else
	note "llvm-objdump absent -- skipping the Mach-O shape checks"
fi

# --- 4. the vendored backend is the one that will actually run ------------

step_no=4
[ -f "${VENDORED}" ] || die "${step_no}" "vendored backend missing: ${VENDORED}" ""
[ -f "${INSTALLED}" ] || die "${step_no}" \
	"backend not installed in the overlay: ${INSTALLED}" ""
sha_installed="$(sha256 -q "${INSTALLED}" 2>/dev/null || shasum -a 256 "${INSTALLED}" | cut -d' ' -f1)"
sha_vendored="$(sha256 -q "${VENDORED}" 2>/dev/null || shasum -a 256 "${VENDORED}" | cut -d' ' -f1)"
[ "${sha_installed}" = "${sha_vendored}" ] || die "${step_no}" \
	"overlay and vendored backend differ (${sha_installed} vs ${sha_vendored})" ""
[ "${sha_vendored}" = "${EXPECT_SHA}" ] || die "${step_no}" \
	"vendored backend is not the committed artifact (${sha_vendored})" ""
ok "backend sha256 $(printf '%s' "${sha_vendored}" | cut -c1-8)... matches vendored, overlay and the committed hash"

printf '\n=== preflight: %d check(s) passed ===\n' "${npass}"

# --- 5. raise the seat, then hold it up for the duration of the run -------

step_no=5
if [ "${NO_SEAT}" = "1" ]; then
	printf '\n=== seat ===\n'
	note "NO_SEAT=1 -- skipped, no seat is raised and none is needed"
else
printf '\n=== seat ===\n'
printf 'before: %s\n' "$(SWAYSOCK="${SWAYSOCK}" swaymsg -t get_seats \
	| tr -d ' \n' | sed 's/.*"capabilities":\([0-9]*\).*/capabilities=\1/')"

XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" \
	"${CONJURE}" "$((WAIT_SECS * 3))" >"${CONJURE_LOG}" 2>&1 &
CONJURE_PID=$!
note "conjure-wayland-input started, pid ${CONJURE_PID}, log ${CONJURE_LOG}"

seat_caps=""
i=0
while [ "${i}" -lt "${WAIT_SECS}" ]; do
	seat_caps="$(SWAYSOCK="${SWAYSOCK}" swaymsg -t get_seats 2>/dev/null \
		| tr -d ' \n' | sed 's/.*"capabilities":\([0-9]*\).*/\1/')"
	# 3 = POINTER(1) | KEYBOARD(2). The shm format negotiation declines to
	# guess when the seat advertises nothing, so 0 is not good enough.
	if [ "${seat_caps}" = "3" ]; then
		break
	fi
	sleep 1
	i=$((i + 1))
done
[ "${seat_caps}" = "3" ] || die "${step_no}" \
	"seat never reached capabilities=3 (stuck at '${seat_caps:-?}')" \
	"WaylandDisplay: no wl_output mode known yet, reporting a placeholder" \
	# conjure is stopped by the trap
ok "seat capabilities=3 (keyboard+pointer)"
fi # NO_SEAT

# --- 6. the run itself ----------------------------------------------------

step_no=6

# DYLD_PRINT_* for the guest's dyld.
#
# The single root prompt this project has left is worth spending on attribution
# as well as on the result, so the guest's loader is asked to narrate itself.
# These are not guesses: all nineteen names below were read out of the dyld
# that actually runs, and each one is referenced exactly once from
# dyld::processDyldEnvironmentVariable -- leaq <string>,%rsi ; callq _strcmp ;
# movb $0x1,<flag> -- which is this dyld actually reading the variable and
# setting a bit, not merely carrying the string. Two variables that
# launch-dynamic sets for its own runs, DYLD_PRINT_FILES and
# DYLD_PRINT_SEARCHING, are NOT in that set: neither string exists anywhere in
# the binary, so this dyld ignores them. They are dyld4-era names.
#
# The ones that carry the attribution, and why:
#   DYLD_PRINT_LIBRARIES           image load order; the last "dyld: loaded:"
#                                  line before a fault names the suspect image
#   DYLD_PRINT_BINDINGS            every symbol binding, i.e. the imports being
#   DYLD_PRINT_WEAK_BINDINGS       resolved -- the closest thing to naming the
#                                  lookup that was in progress
#   DYLD_PRINT_APIS                the dyld API surface, so dlopen() calls show up
#   DYLD_PRINT_INTERPOSING         interposing tuples
#   DYLD_PRINT_SEGMENTS            segment layout of what did load
#   DYLD_PRINT_STATISTICS          counts of each stage
#   DYLD_PRINT_STATISTICS_DETAILS  the same, broken down
#   DYLD_PRINT_RPATHS, _WARNINGS, _INITIALIZERS, _DOFS, _OPTS, _ENV,
#   DYLD_PRINT_CODE_SIGNATURES     the rest of the set, for completeness
#   DYLD_PRINT_REBASINGS           slide/rebase decisions
#
# DYLD_PRINT_TO_STDERR rather than DYLD_PRINT_TO_FILE on purpose: the loader's
# narration and the mldr handler's crash output (backtrace and guest stack
# dump) have to land on the SAME stream, because their relative order is part
# of the evidence. Splitting them across two files would throw that away.
# DYLD_PRINT_TO_FILE exists and is read too, but it redirects dyld's own output
# to a path given as the variable's value.
#
# They go in as `env NAME=value` arguments rather than being exported, for two
# reasons: launch-dynamic only ever *adds* to the environment it inherited and
# then execs mldr, so anything it holds reaches the guest; and sudo resets the
# environment by default, which only explicit `env` arguments survive.
DYLD_TRACE="DYLD_PRINT_LIBRARIES DYLD_PRINT_LIBRARIES_POST_LAUNCH \
DYLD_PRINT_BINDINGS DYLD_PRINT_WEAK_BINDINGS DYLD_PRINT_APIS \
DYLD_PRINT_INTERPOSING DYLD_PRINT_SEGMENTS DYLD_PRINT_STATISTICS \
DYLD_PRINT_STATISTICS_DETAILS DYLD_PRINT_RPATHS DYLD_PRINT_WARNINGS \
DYLD_PRINT_INITIALIZERS DYLD_PRINT_DOFS DYLD_PRINT_OPTS DYLD_PRINT_ENV \
DYLD_PRINT_CODE_SIGNATURES DYLD_PRINT_REBASINGS DYLD_PRINT_TO_STDERR"

RUN_CMD="env DARLING_SRC_DIR=${SRC} DARLING_OVERLAY=${OD} DARLING_BUILD_DIR=${BD}"
RUN_CMD="${RUN_CMD} DARLING_TEST_BINARY=${TEST_BIN}"
# The staging list the harness stages, derived in 3b from the dylib closure.
# Passed explicitly so the harness never falls back to its built-in list on this
# path -- the fallback is announced in its output, and a run that printed it
# would be staging a guess.
RUN_CMD="${RUN_CMD} DARLING_STAGING_TREES=${STAGING_LIST}"
if [ "${NO_SEAT}" = "1" ]; then
	# No WAYLAND_DISPLAY / XDG_RUNTIME_DIR: passing them would hand the guest
	# a seat that does not exist and make a later wl_display_connect failure
	# ambiguous — is it the code, or the environment we invented?
	note "NO_SEAT=1 -- WAYLAND_DISPLAY and XDG_RUNTIME_DIR are NOT passed to the guest"
else
	RUN_CMD="${RUN_CMD} WAYLAND_DISPLAY=${WAYLAND_DISPLAY} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR}"
fi
for v in ${DYLD_TRACE}; do
	RUN_CMD="${RUN_CMD} ${v}=1"
done
RUN_CMD="${RUN_CMD} ${BD}/launch-dynamic"

# The log has to be able to survive long after the run, and it must never be
# something a `git add -A` can pick up, so it is kept outside the source tree
# and that is checked rather than assumed.
case "${LOG}" in
	"${SRC}"/*)
		die "${step_no}" "the log path ${LOG} is inside the source tree; refusing" \
			"set DARLING_BUILD_DIR to somewhere outside ${SRC}" ;;
esac

printf '\n=== run ===\n'
printf 'log: %s\n' "${LOG}"
printf 'dyld trace: %d variable(s) set, output interleaved with the crash output\n' \
	"$(printf '%s\n' ${DYLD_TRACE} | wc -l | tr -d ' ')"
if [ "${DRY_RUN}" = "1" ]; then
	printf 'DRY_RUN=1 -- NOT running. The command that would be run is exactly:\n\n'
	printf '    sudo %s\n\n' "${RUN_CMD}"
	printf 'The virtual input devices will exist only while this script runs.\n'
	printf 'The log would be written to:\n    %s\n' "${LOG}"
	printf '(outside the source tree, so it is never committable)\n'
	exit 0
fi

printf 'running: sudo %s\n' "${RUN_CMD}"

# The redirection below runs in THIS shell, before sudo does anything, so it is
# performed with the caller's privileges. A log left there by a run that had
# more of them — a root run of this very script — cannot be reopened for
# writing: the redirection fails, the `|| true` two lines down swallows the
# error, and every grep after that reads the PREVIOUS run's log as though the
# verdict belonged to this one. Nothing about that looks like a permissions
# problem, which is what makes it worth catching here instead.
#
# Moved aside, not deleted: that file is the only record of the run that made
# it, and leaving evidence behind is what this script is for. It is also the
# file the preflight failures above would otherwise have quoted.
if [ -e "${LOG}" ] && ! ( : >>"${LOG}" ) 2>/dev/null; then
	stale="${LOG}.foreign-$(id -un)-$(date +%Y%m%d-%H%M%S)"
	if mv -f "${LOG}" "${stale}" 2>/dev/null; then
		printf 'previous log belongs to another user; moved aside to:\n    %s\n' \
			"${stale}"
	else
		printf '\nFAILED: %s is not writable by this user and could not be moved aside.\n' \
			"${LOG}" >&2
		printf 'Remove or rename it, or point DARLING_BUILD_DIR somewhere else.\n' >&2
		exit 1
	fi
fi
if ! ( : >>"${LOG}" ) 2>/dev/null; then
	printf '\nFAILED: %s cannot be created by this user.\n' "${LOG}" >&2
	printf 'Point DARLING_BUILD_DIR at a directory this user can write to.\n' >&2
	exit 1
fi

# Both streams, and no line buffering games: the loader writes from a child
# process and the handler writes from the guest, and losing either half to a
# full pipe would defeat the point. stdout and stderr are already merged into
# one file by the redirection, which is what makes the interleaving meaningful.
# shellcheck disable=SC2086
sudo ${RUN_CMD} >"${LOG}" 2>&1 || true
LOG_OWNS_LOG=1

# launch-dynamic exits 0 even when the guest dies, so the log decides, not $?.
grep -q 'RESULT: window created' "${LOG}" 2>/dev/null || die "${step_no}" \
	"the run did not reach the shm buffer (see below)" \
	"WaylandWindow: shm allocation failed for %dx%d buffer: %s
    WaylandWindow: mmap failed for %dx%d buffer: %s
    WaylandWindow: wl_shm_pool_create_buffer failed for %dx%d
    WaylandWindow: wl_compositor_create_surface failed
    WaylandWindow: wl_display_dispatch failed while waiting for initial configure: %s
    WaylandWindow: flushBuffer: no buffer available, frame dropped"

printf '\n=== result ===\n'
grep -E '^\[step|RESULT|NEW cwd entry' "${LOG}" 2>/dev/null || true
ok "the probe reached the shm buffer and flushed it"

# The log is the product of this run even when the run fails, so its path is
# echoed last, after the trap has already cleaned up. It sits outside the
# source tree on purpose: it is a machine-local artefact full of addresses and
# paths, and nothing about it belongs in the repository.
printf '\nfull log (loader trace + mldr handler output, interleaved):\n    %s\n' "${LOG}"
printf 'size: %s bytes, %s lines\n' \
	"$(wc -c <"${LOG}" | tr -d ' ')" "$(wc -l <"${LOG}" | tr -d ' ')"
exit 0
