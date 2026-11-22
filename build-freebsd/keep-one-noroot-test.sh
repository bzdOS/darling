#!/bin/sh
# keep-one-check.sh without KEEPONE_ROOT/DARLING_BUILD_DIR: one line "KEEP-ONE usage error: KEEPONE_ROOT or DARLING_BUILD_DIR required", rc 2, all modes.
D=$(dirname "$0")
v=pass
for m in "" --list --apply --clean-sockets; do
	out=$(env -u KEEPONE_ROOT -u DARLING_BUILD_DIR sh "$D/keep-one-check.sh" $m); rc=$?
	[ "$out" = "KEEP-ONE usage error: KEEPONE_ROOT or DARLING_BUILD_DIR required" ] && [ $rc = 2 ] || { v=fail; echo "mode '$m': rc=$rc out=$out"; }
done
R=$(mktemp -d /opt/darling/build/scratch/keepone-noroot.XXXXXX) || exit 2
mkdir -p "$R/build" "$R/backup"
out=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --list); rc=$?
[ -z "$out" ] && [ $rc = 0 ] || v=fail
out=$(env -u DARLING_BUILD_DIR KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh")
case "$out" in "KEEP-ONE verdict="*) ;; *) v=fail; echo "fake root: $out";; esac
rmdir "$R/build" "$R/backup" "$R"
echo "KEEPONE-NOROOT-TEST $v"
[ $v = pass ]
