#!/bin/sh
# keep-one-check.sh: series files under 1 KB are not series; they do not push a real series out of the latest two.
D=$(dirname "$0")
R=$(mktemp -d /tmp/keepone-small.XXXXXX) || exit 2
mkdir -p "$R/build" "$R/backup"; B=$R/build
for p in a b c; do head -c 2000 /dev/zero >"$B/$p-series.out"; done
touch -t 202001010000 "$B/a-series.out"; touch -t 202001020000 "$B/b-series.out"; touch -t 202001030000 "$B/c-series.out"
echo x >"$B/tiny-series.out"
out=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh")
v=pass
case "$out" in *"series=c,b "*) ;; *) v=fail; echo "$out";; esac
rm -f "$B"/*-series.out; rmdir "$B" "$R/backup" "$R"
echo "KEEPONE-SMALL-TEST $v"
[ $v = pass ]
