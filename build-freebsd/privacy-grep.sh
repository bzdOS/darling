#!/bin/sh
# privacy-grep.sh - the two public-repo greps for BASE..BRANCH. Prints PRIVACY OK or the offending lines.
# usage: privacy-grep.sh BASE BRANCH   (run inside the repository)
# the pattern is assembled from pieces so that this file does not match itself
P='plan''ck|bsdos-x''86|agent-1''85|darling-he''ad|darling-le''ad|task-[0-9]+|fedora-[0-9]+|178\.72\.|192\.168\.|10\.77\.|/srv/fl''eet|/mnt/bs''dos|/ro''ot/|/op''t/'
a=$(git diff "$1".."$2" | grep -E '^\+' | grep -nE "$P")
b=$(git log --format=%B "$1".."$2" | grep -nE "$P")
if [ -z "$a$b" ]; then echo "PRIVACY OK"; else echo "PRIVACY FAIL"; echo "$a"; echo "$b"; fi
