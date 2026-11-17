#!/bin/sh
# mldr-socket-leak-test.sh - run N short darling commands, count NEW /tmp/darling-mldr-<pid>-<fd> sockets whose pid is dead.
#   MLDR-SOCKET-LEAK-VERDICT pass|fail runs=N leaked=K     (pass = 0 leaked; needs the fixed mldr deployed)
# Usage: sh build-freebsd/mldr-socket-leak-test.sh [N]   Env: DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR (as run-fork-fail-probe.sh)
# The short command is run-fork-fail-probe.sh (fork-fail-probe-macho); only sockets absent before the runs count.
HERE=$(cd "$(dirname "$0")" && pwd); N="${1:-${MLDRSOCK_RUNS:-2}}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
snap() { ls /tmp | grep -E "^darling-mldr-[0-9]+-[0-9]+$" | sort; }
B=$(mktemp -t mldrsock.XXXXXX); A=$(mktemp -t mldrsock.XXXXXX); trap "rm -f $B $A" EXIT
snap >"$B"; i=0
ran=0
while [ "$i" -lt "$N" ]; do
	sh "$HERE/run-fork-fail-probe.sh" 60 2>&1 | grep -aq "^FORK-VERDICT" && ran=$((ran+1)); i=$((i+1))
done
sleep 2; snap >"$A"; k=0
for f in $(comm -13 "$B" "$A"); do
	pid=${f#darling-mldr-}; pid=${pid%%-*}
	[ -S "/tmp/$f" ] && ! kill -0 "$pid" 2>/dev/null && k=$((k+1))
done
v=pass; { [ "$k" -gt 0 ] || [ "$ran" -lt "$N" ]; } && v=fail
echo "MLDR-SOCKET-LEAK-VERDICT $v runs=$N leaked=$k"
[ $v = pass ]
