#!/bin/sh
# privacy-grep.sh - the two public-repo greps for BASE..BRANCH. Prints PRIVACY OK or the offending lines.
# usage: privacy-grep.sh BASE BRANCH   (run inside the repository)
P='planck|bsdos-x86|agent-185|darling-head|darling-lead|task-[0-9]+|fedora-[0-9]+|178\.72\.|192\.168\.|10\.77\.|/srv/fleet|/mnt/bsdos|/root/|/opt/'
a=$(git diff "$1".."$2" | grep -E '^\+' | grep -nE "$P")
b=$(git log --format=%B "$1".."$2" | grep -nE "$P")
if [ -z "$a$b" ]; then echo "PRIVACY OK"; else echo "PRIVACY FAIL"; echo "$a"; echo "$b"; fi
