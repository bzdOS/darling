#!/bin/sh
# keep-one-prefile-test.sh - a gate's own temp file (stage-gate-pre.<rand>) with a LIVE owner in a role TMPDIR is
# not a keep-one candidate and survives every keep-one run. This is the reproduction of the report "keep-one
# sweeps stage-gate temp files without a liveness check": measured on the current script the file is not even
# named by --list (candidate sources are <root>/build, <root>/backup, the stray dirs, the scratch globs, the src
# tree and the patch dir), so no keep-one mode can delete it; --apply additionally refuses any path outside
# <root>/build and <root>/backup.
#   Prints one line:  KEEP-ONE-PREFILE-TEST pass|fail [failed checks]      exit 0 on pass.
# Env: KEEPONE_ROOT is built under $DARLING_BUILD_DIR/scratch (the real tree is never touched); TMPDIR names the
# dir the pre-file is created in (the role temp dir by default, as the gate sees it).
D=$(dirname "$0"); PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
BD="${DARLING_BUILD_DIR:-/opt/darling/build}"
mkdir -p "$BD/scratch" || { echo "KEEP-ONE-PREFILE-TEST fail no-scratch"; exit 1; }
R=$(mktemp -d "$BD/scratch/keepone-prefile.XXXXXX") || { echo "KEEP-ONE-PREFILE-TEST fail mktemp"; exit 1; }
trap 'rm -rf "$R"; rm -f "$PRE"' EXIT
mkdir -p "$R/build" "$R/backup" || { echo "KEEP-ONE-PREFILE-TEST fail mkdir"; exit 1; }
bad=""; ck() { # name rc-of-the-condition(0 = ok)
	[ "$2" = 0 ] || bad="$bad $1"
}

# fresh pre-file, owner = this live shell (the gate's orphan snapshot is taken and read by the same pid)
PRE=$(mktemp "${TMPDIR:-/tmp}/stage-gate-pre.XXXXXX") || { echo "KEEP-ONE-PREFILE-TEST fail mktemp-pre"; exit 1; }
kill -0 $$; ck owner-alive $?
[ -f "$PRE" ]; ck pre-created $?

# 1. --list must not name it (default env: no KEEPONE_SCRATCH_TMP override)
out=$(env KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --list 2>&1); rc=$?
[ "$rc" = 0 ]; ck list-rc $?
printf '%s\n' "$out" | grep -qF "$PRE" && ck list-names-pre 1 || ck list-names-pre 0
[ -f "$PRE" ]; ck pre-alive-after-list $?

# 2. --clean-sockets (the one cleanup the gate runs) leaves it
env KEEPONE_ROOT="$R" sh "$D/keep-one-check.sh" --clean-sockets >/dev/null 2>&1; rc=$?
[ "$rc" = 0 ]; ck clean-sockets-rc $?
[ -f "$PRE" ]; ck pre-alive-after-clean-sockets $?

# 3. --apply against the sandbox root, scratch globs pointed inside it so no foreign /tmp file is at stake
env KEEPONE_ROOT="$R" KEEPONE_SCRATCH_TMP="$R/nosuch-scratch/x-*.py" sh "$D/keep-one-check.sh" --apply >/dev/null 2>&1; rc=$?
[ "$rc" = 0 ] || [ "$rc" = 4 ]; ck apply-rc $?
[ -f "$PRE" ]; ck pre-alive-after-apply $?

# 4. the no-root call still says usage, not a verdict (the third root of the same task)
out=$(env -u KEEPONE_ROOT -u DARLING_BUILD_DIR sh "$D/keep-one-check.sh" --list 2>&1); rc=$?
[ "$rc" = 2 ]; ck noroot-rc $?
case "$out" in "KEEP-ONE usage error: KEEPONE_ROOT or DARLING_BUILD_DIR required") true;; *) false;; esac; ck noroot-line $?
case "$out" in *"verdict="*) false;; *) true;; esac; ck noroot-no-verdict $?

if [ -z "$bad" ]; then echo "KEEP-ONE-PREFILE-TEST pass"; exit 0; fi
echo "KEEP-ONE-PREFILE-TEST fail$bad"; exit 1
