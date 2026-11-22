#!/bin/sh
# cfinit-check.sh - the overlay CoreFoundation must run __CFInitialize when it is loaded.
# Static: the image has an LC_ROUTINES_64 command, or a __mod_init_func entry that resolves to ___CFInitialize
#   (ld64.lld drops "-init"; build-cf-only.sh links a __mod_init_func stub for it).
# Dynamic: tests/src/cfinit-probe.c is built into tests/cfinit-probe-macho and run under launch-dynamic;
#   it reads __CFInitialized and CFStringGetTypeID() right after dlopen of CoreFoundation.
# Usage: sh build-freebsd/cfinit-check.sh [--static] [wait-secs]    (--static: only the image check, no guest run; exit 0 if ok)
#    Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR
# Prints "CFINIT-STATIC ok|FAIL ..." and one verdict line "CFINIT-VERDICT pass|FAIL initialized=<n> string_typeid=<n> recall=<..> static=<ok|none>";
# exit 0 on pass. Only the darlingserver processes this run started are stopped (by pid).
STATIC_ONLY=0; [ "$1" = "--static" ] && { STATIC_ONLY=1; shift; }
SRC="${DARLING_SRC_DIR:-}"; BD="${DARLING_BUILD_DIR:-}"; OD="${DARLING_OVERLAY:-}"; WAIT="${1:-120}"
if [ -z "$SRC" ] || [ -z "$BD" ] || [ -z "$OD" ]; then
	echo "CFINIT-VERDICT FAIL initialized=-1 string_typeid=0 [missing env:$([ -z "$SRC" ] && printf " DARLING_SRC_DIR")$([ -z "$BD" ] && printf " DARLING_BUILD_DIR")$([ -z "$OD" ] && printf " DARLING_OVERLAY")]"; exit 1
fi
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=cfinit-probe-macho; W=""
CF="$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"
st=none
if llvm-objdump --macho --private-headers "$CF" 2>/dev/null | grep -q "LC_ROUTINES"; then st=ok
elif llvm-objdump --macho -s --section=__DATA,__mod_init_func "$CF" 2>/dev/null | grep -q "___CFInitialize\$"; then st=ok; fi
if [ "$st" = ok ]; then echo "CFINIT-STATIC ok (LC_ROUTINES or __mod_init_func entry for ___CFInitialize)"
else echo "CFINIT-STATIC FAIL no LC_ROUTINES and no ___CFInitialize in __mod_init_func: $(llvm-objdump --macho -s --section=__DATA,__mod_init_func "$CF" 2>/dev/null | awk "NF>=3{printf \"%s \", \$3}")"; fi
if [ "$STATIC_ONLY" = 1 ]; then [ "$st" = ok ]; exit $?; fi
W=$(mktemp -d "$BD/cfinit.XXXXXX") || { echo "FATAL: mktemp workdir failed for cfinit" >&2; exit 1; }
cleanup() { rm -rf "$W"; }
trap cleanup EXIT INT TERM HUP
[ -d "$SDK_FLAT/usr/include" ] || { echo "CFINIT-VERDICT FAIL initialized=-1 string_typeid=0 [no flattened SDK]"; exit 1; }
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/cfinit-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/libSystem.B.dylib" 2>/dev/null \
	|| { echo "CFINIT-VERDICT FAIL initialized=-1 string_typeid=0 [build]"; exit 1; }
LOG="$BD/cfinit-check-$(date +%m%d-%H%M%S).log"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
. "$(dirname "$0")/probe-launch.sh"
probe_launch "$LOG" "$WAIT" "CFINIT-VERDICT\|FATAL signal" sudo timeout -k 5 "$WAIT" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_SMOKE_REFRESH=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
grep -a "^CFINIT-PROBE" "$LOG" | tail -1
if grep -qa "^CFINIT-VERDICT" "$LOG"; then
	v=$(grep -a "^CFINIT-VERDICT" "$LOG" | tail -1)
	if [ "$st" = ok ]; then echo "$v static=ok"; else echo "$(echo "$v" | sed "s/CFINIT-VERDICT [a-zA-Z]*/CFINIT-VERDICT FAIL/") static=none"; exit 1; fi
	case "$v" in "CFINIT-VERDICT pass"*) exit 0;; esac; exit 1
fi
echo "CFINIT-VERDICT FAIL initialized=-1 string_typeid=0 static=$st [guest died or timed out]"; exit 1
