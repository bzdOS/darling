#!/bin/sh
# null-const-probe.sh - NULL-CONST: string constants the Chrome framework imports must not be zero objects
# Static check of the overlay (no run, no lock).  Usage: sh null-const-probe.sh [overlay-dir]   Env: DARLING_SRC_DIR DARLING_OVERLAY
# One verdict line: NULL-CONST pass|fail strings=N other=M from_memory=X name_fallback=Y cocotron=Z; exit 0 on pass.
SRC="${DARLING_SRC_DIR:?}"; OD="${1:-${DARLING_OVERLAY:?}}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
l=$(python3 -I "$SRC/build-freebsd/data-shadow-census.py" chrome-imports --overlay "$OD" ${NULLCONST_CHROME_ROOT:+--chrome-root "$NULLCONST_CHROME_ROOT"} 2>/dev/null | grep -a "^NULL-CONST-COUNT" | tail -1)
c=${l#NULL-CONST-COUNT }
case "$c" in strings=0\ *) echo "NULL-CONST pass $c"; exit 0;; strings=*) echo "NULL-CONST fail $c"; exit 1;; esac
echo "NULL-CONST fail strings=? other=? from_memory=? name_fallback=? cocotron=?"; exit 1
