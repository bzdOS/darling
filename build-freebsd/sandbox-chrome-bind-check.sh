#!/bin/sh
# sandbox-chrome-bind-check.sh - the libsandbox API (sandbox_apply, sandbox_init_with_parameters, sandbox_compile_string,
# sandbox_create_params, ...) that the Chrome framework and helper bind from /usr/lib/libsandbox.1.dylib is linked two-level
# against that image, as Chrome does (no dlsym), and must succeed as a no-op.
# Builds tests/sandbox-chrome-bind-probe-macho from tests/src/sandbox-chrome-bind-probe.c and runs it under launch-dynamic.
# Usage: sh build-freebsd/sandbox-chrome-bind-check.sh [wait-secs]    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints SBXBIND lines and a final "SBXBIND RESULT pass|FAIL"; exit 0 on pass, 1 otherwise.
# Only the darlingserver processes this run started are stopped (by pid); a pre-existing one is left alone.
SRC="${DARLING_SRC_DIR:?}"; BD="${DARLING_BUILD_DIR:?}"; OD="${DARLING_OVERLAY:?}"; WAIT="${1:-60}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
T=sandbox-chrome-bind-probe-macho; W="$BD/sc-stubs"; mkdir -p "$W"
clang -target x86_64-apple-macos10.12 -O1 -w -ffreestanding -c "$SRC/tests/src/sandbox-chrome-bind-probe.c" -o "$W/sbxprobe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/sbxprobe.o" "$OD/usr/lib/libsandbox.1.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "SBXBIND RESULT FAIL (build: undefined sandbox symbol or compile error)"; exit 1; }
LOG="$BD/sandbox-chrome-bind-check.log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "SBXBIND DONE\|FATAL signal" sudo timeout -k 5 120 env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^SBXBIND" "$LOG"
if grep -qa "^SBXBIND DONE fail=0" "$LOG" && ! grep -qa "^SBXBIND .* FAIL" "$LOG"; then echo "SBXBIND RESULT pass"; exit 0; fi
echo "SBXBIND RESULT FAIL"; exit 1
