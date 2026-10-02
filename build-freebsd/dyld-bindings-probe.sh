#!/bin/sh
# dyld-bindings-probe: run the guest-wl-session-roundtrip probe with
# DYLD_PRINT_BINDINGS=1 (alongside the current LD_DEBUG=all) so the log
# carries dyld's binding trace. Grep it for a symbol's resolve.
#
# Usage: sh build-freebsd/dyld-bindings-probe.sh [SYMBOL]
#   default SYMBOL: _OBJC_METACLASS_$_NSObject
# Output: $DARLING_BUILD_DIR/wl-body-dyldbind.log (the probe log)
#         $DARLING_BUILD_DIR/wl-body-dyldbind.txt (grep of SYMBOL + exit)
BD=${DARLING_BUILD_DIR:-/tmp/darling-build}
SRC=${DARLING_SRC_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
OVL=${DARLING_OVERLAY:-${SRC}/overlay}
SYM=${1:-_OBJC_METACLASS_\$_NSObject}
LOG="${BD}/wl-body-dyldbind.log"
DUMP="${BD}/wl-body-dyldbind.txt"
: > "${LOG}"; : > "${DUMP}"

cd /tmp/wlrun || exit 97
sudo -n env DARLING_SRC_DIR="${SRC}" \
	DARLING_OVERLAY="${OVL}" \
	DARLING_BUILD_DIR="${BD}" \
	DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
	DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
	LD_LIBRARY_PATH="${BD}/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
	DYLD_PRINT_BINDINGS=1 \
	XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
	env LD_DEBUG=all \
	timeout --foreground -k 5 200 "${BD}/launch-dynamic" \
	> "${LOG}" 2>&1
RC=$?
echo "probe exit=$RC" >> "${DUMP}"
echo "=== log lines: $(wc -l < "${LOG}") ===" >> "${DUMP}"
echo "=== markers ===" >> "${DUMP}"
grep -aoE '\[step [0-9]+\]|DID-NOT-RETURN|LANE FINDING' "${LOG}" | uniq >> "${DUMP}"
echo "=== DYLD_PRINT_BINDINGS lines (any) ===" >> "${DUMP}"
grep -aE 'bind|Bind|BIND' "${LOG}" | grep -avE 'reloc|lm_|lmp_|lml_|dlsym_fatal|__cxa|rtld' | head -40 >> "${DUMP}"
echo "=== SYMBOL '${SYM}' occurrences ===" >> "${DUMP}"
grep -an -- "${SYM}" "${LOG}" | head -40 >> "${DUMP}"
echo "=== last 20 log lines ===" >> "${DUMP}"
tail -20 "${LOG}" >> "${DUMP}"
echo "DONE" >> "${DUMP}"
