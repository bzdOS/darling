#!/bin/sh
# resume-selftest.sh - lost-wakeup selftest for darlingserver's Thread::resume/suspend.
# usage: resume-selftest.sh [DARLINGSERVER_BINARY] [SUSPEND_DELAY_US]   (default binary: $DARLING_BUILD_DIR/dserver/darlingserver)
# Starts the server in a private temp prefix with DSERVER_RESUME_SELFTEST=1 (and optionally
# DSERVER_SELFTEST_SUSPEND_DELAY_US to force the race); it runs bursts of kernelAsync lambdas and
# exits itself. Last line: RESUME-SELFTEST PASS|FAIL <detail>. Exit 0 only on PASS. Hard limit 300 s.
BIN=${1:-${DARLING_BUILD_DIR:?}/dserver/darlingserver}; D=${2:-0}
. "${DARLING_SRC_DIR:?}/build-freebsd/dserver-prefix.sh"
dsp_new || exit 1
OUT=$(mktemp /tmp/resume-selftest.XXXXXX)
/usr/local/bin/sudo -n env DSERVER_RESUME_SELFTEST=1 DSERVER_SELFTEST_SUSPEND_DELAY_US="$D" \
  timeout -k 5 300 "$BIN" "$DARLING_PREFIX" 0 0 3 0 3>/dev/null > "$OUT" 2>&1
rc=$?
V=$(grep -a '^RESUME-SELFTEST' "$OUT" | tail -1)
[ -n "$V" ] || V="RESUME-SELFTEST FAIL: no verdict (rc=$rc), tail: $(tail -c 200 "$OUT" | tr '\n' ' ')"
echo "$V [binary=$(basename "$BIN") delay_us=$D]"
rm -f "$OUT"
case "$V" in *PASS*) exit 0;; *) exit 1;; esac
