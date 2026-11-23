#!/bin/sh
# keep-one-check.sh: build/commitmsg-*.txt (the message file a series writes for its commit) is a scratch
# candidate of section 6 — listed by --list, counted in scratch_files, removable by --apply — and no other
# .txt in build/ is. Written once and never re-read, so a series must not leave it for a hand cleanup.
D=$(dirname "$0")
R=$(mktemp -d /tmp/keepone-commitmsg.XXXXXX) || exit 2
mkdir -p "$R/build" "$R/backup"
echo x >"$R/build/commitmsg-appkit-kg.txt"; echo x >"$R/build/commitmsg-561d.txt"; echo x >"$R/build/notes.txt"
touch -t 202001010000 "$R"/build/*.txt
out=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --list); v=pass
case "$out" in *"$R/build/commitmsg-appkit-kg.txt"*) ;; *) v=fail; echo "no commitmsg-appkit-kg: $out";; esac
case "$out" in *"$R/build/commitmsg-561d.txt"*) ;; *) v=fail; echo "no commitmsg-561d: $out";; esac
case "$out" in *"$R/build/notes.txt"*) v=fail; echo "notes.txt listed: $out";; esac
kv=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" 2>&1)
case "$kv" in *" scratch_files=2"*) ;; *) v=fail; echo "verdict: $kv";; esac
out=$(KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --apply-check 2>&1)
case "$out" in "KEEP-ONE apply-check ok paths=2") ;; *) v=fail; echo "apply-check: $out";; esac
rm -f "$R/build/commitmsg-appkit-kg.txt" "$R/build/commitmsg-561d.txt" "$R/build/notes.txt"
rmdir "$R/build" "$R/backup" "$R"
echo "KEEPONE-COMMITMSG-TEST $v"
[ $v = pass ]