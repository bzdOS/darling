#!/bin/sh
# guest-abort-symbolize-selftest.sh - a log with a delivered SIGTRAP (no ABORT-TRACE, no FATAL block) must give
# an IMAGES line (load bases) and an ABORT line whose frames are image+offset (ChromeFW+0x...).
# Prints: SYMBOLIZE-TRAP-VERDICT pass|fail <detail>
W=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp "${TMPDIR:-/tmp}/sym-selftest.XXXXXX.log") || exit 2
cat > "$T" <<'LOG'
[dyld-trace]   [0] mh=0x827d8b000 chrome-macho <-NEW
[dyld-trace]   [44] mh=0x33b8ebf52000 Google Chrome for Testing Framework <-NEW
[darling-mldr] SIGTRAP-AUDIT-DELIVER: native SIGTRAP -> guest handler=0x1 (linux_signo=5) rip=0x33b8ed0f2ecf si_code=1 si_addr=0x33b8ed0f2ecf
[darling-mldr] SIGTRAP-AUDIT-CHAIN: rsp=0x7fffffdfdbd0 rbp=0x7fffffdfdc60
[darling-mldr] SIGTRAP-AUDIT-CHAIN: #0 ret=0x33b8ee3b0c5e
WRAPPER_RC=133
LOG
OUT=$(sh "$W/guest-abort-symbolize.sh" "$T" 2>&1); rc=$?
rm -f "$T"
if echo "$OUT" | grep -q 'IMAGES .*ChromeFW@0x33b8ebf52000' && echo "$OUT" | grep -q 'frames=ChromeFW+0x11a0ecf > ChromeFW+0x245ec5e'; then
	echo "SYMBOLIZE-TRAP-VERDICT pass rc=$rc"; exit 0
fi
echo "SYMBOLIZE-TRAP-VERDICT fail rc=$rc $(echo "$OUT" | head -c 300)"; exit 1
