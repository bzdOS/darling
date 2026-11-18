#!/bin/sh
# keep-one-check.sh must never list build/darling.env for removal, while a stale dummy log is listed.
D=$(dirname "$0")
R=$(mktemp -d /opt/darling/build/scratch/keepone-env.XXXXXX) || exit 2
mkdir -p "$R/build" "$R/backup"
echo x >"$R/build/darling.env"; echo x >"$R/build/dummy-stale.log"
touch -t 202001010000 "$R/build/darling.env" "$R/build/dummy-stale.log"
out=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --list)
v=pass
case "$out" in *darling.env*) v=fail;; esac
case "$out" in *dummy-stale.log*) ;; *) v=fail;; esac
rm -f "$R/build/darling.env" "$R/build/dummy-stale.log"; rmdir "$R/build" "$R/backup" "$R"
echo "KEEPONE-ENV-TEST $v"
[ $v = pass ]
