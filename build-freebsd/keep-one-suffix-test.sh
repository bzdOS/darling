#!/bin/sh
# keep-one-check.sh: series fix23c owns chrome-sr-fix23<N> logs, so a fake root with only those gives no candidates.
D=$(dirname "$0")
R=$(mktemp -d /tmp/keepone-suffix.XXXXXX) || exit 2
mkdir -p "$R/build" "$R/backup"
head -c 2000 /dev/zero >"$R/build/fix23c-series.out"; echo x >"$R/build/chrome-sr-fix231.log"
touch -t 202001010000 "$R/build/fix23c-series.out" "$R/build/chrome-sr-fix231.log"
out=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --list)
v=pass; [ -z "$out" ] || { v=fail; echo "candidates: $out"; }
rm -f "$R/build/fix23c-series.out" "$R/build/chrome-sr-fix231.log"; rmdir "$R/build" "$R/backup" "$R"
echo "KEEPONE-SUFFIX-TEST $v"
[ $v = pass ]
