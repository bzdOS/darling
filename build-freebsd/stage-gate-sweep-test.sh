#!/bin/sh
# stage-gate-sweep-test.sh - the gate's own TMPDIR artifacts swept mid-run: one defined "skipped" line per lost
# artifact, never a raw "cannot open" from a redirection and never a verdict invented from an absent baseline.
#   Prints one line:  STAGE-GATE-SWEEP-TEST pass|fail [failed checks]      exit 0 on pass.
# Reproduction (22.11): one working run lost stage-gate-pre.*, stage-gate-list.* and stage-gate-base.* from the
# role temp dir; the log then carried "stage-gate.sh: cannot open <...>/stage-gate-pre.*" and
# "comm: <...>/stage-gate-base.*: No such file or directory", and an absent baseline was read as "no old pids".
# Covers: orphan_check with the baseline gone (defined line, rc 0, no raw error), stage_check with list+hash gone
# (defined line, rc 0, no raw error), a live baseline still reporting a real orphan and a real mutation (the
# guard masks nothing), and sh -n of the gate.
HERE=$(cd "$(dirname "$0")" && pwd); GATE="$HERE/stage-gate.sh"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
BD="${DARLING_BUILD_DIR:-/opt/darling/build}"
mkdir -p "$BD/scratch" || { echo "STAGE-GATE-SWEEP-TEST fail no-scratch"; exit 1; }
T=$(mktemp -d "$BD/scratch/gsweep-test.XXXXXX") || { echo "STAGE-GATE-SWEEP-TEST fail mktemp"; exit 1; }
trap 'rm -rf "$T"' EXIT
bad=""; ck() { # name rc-of-the-condition(0 = ok)
	[ "$2" = 0 ] || bad="$bad $1"
}

# --- helpers from the gate (same extraction as stage-once-test.sh)
sed -n '/^# BEGIN stage-once helpers/,/^# END stage-once helpers/p' "$GATE" >"$T/helpers.sh"
[ -s "$T/helpers.sh" ]; ck helpers-extract $?
STAGE_FAILED=0; stage_fail() { STAGE_FAILED=1; }
. "$T/helpers.sh"

# --- baseline snapshot gone: defined line, rc 0, and the raw shell error nowhere (stdout or stderr)
out=$(orphan_check FSOPS "$T/stage-gate-pre.swept" 2>&1); rc=$?
[ "$rc" = 0 ]; ck orphan-lost-rc $?
case "$out" in "PROBE-ORPHAN skipped FSOPS (baseline snapshot lost mid-run"*) true;; *) false;; esac; ck orphan-lost-line $?
case "$out" in *"cannot open"*|*"No such file"*) false;; *) true;; esac; ck orphan-lost-no-raw $?
[ "$STAGE_FAILED" = 0 ]; ck orphan-lost-not-red $?

# --- a live baseline still reports a real orphan (the guard does not mask it)
PSF="$T/ps.txt"; STAGE_GATE_PS_CMD="cat '$PSF'"; STAGE_GATE_ORPHAN_WAIT=0
export STAGE_GATE_PS_CMD STAGE_GATE_ORPHAN_WAIT
echo "100 1 Tue Nov 18 10:00:00 2026 /x/launch-dynamic" >"$PSF"
orphan_begin; ck orphan-begin $?
out=$(orphan_check FSOPS "$_OPRE" 2>&1); rc=$?
[ "$rc" = 0 ] && [ -z "$out" ]; ck orphan-live-baseline $?
echo "200 100 Tue Nov 18 10:01:00 2026 /x/mldr-real/mldr /tmp/y/probe-macho" >>"$PSF"
out=$(orphan_check FSOPS "$_OPRE" 2>&1); rc=$?
[ "$rc" = 1 ] && case "$out" in *"PROBE-ORPHAN FAIL FSOPS pids=200 "*"PROBE-ORPHAN-PS 200 100 "*) true;; *) false;; esac; ck orphan-reported $?
rm -f "$_OPRE"		# the sweep takes the snapshot between two comparisons
out=$(orphan_check FSOPS "$_OPRE" 2>&1); rc=$?
[ "$rc" = 0 ]; ck orphan-swept-midrun-rc $?
case "$out" in *"cannot open"*|*"No such file"*) false;; *) true;; esac; ck orphan-swept-midrun-no-raw $?

# --- staged overlay baseline gone: defined line, rc 0, no raw error, gate not reddened
SO="$T/overlay"; mkdir -p "$SO/usr/lib"; echo one >"$SO/usr/lib/libone.dylib"
SLIST="$T/list"; SBASE="$T/base"; SAFTR="$T/aftr"
stage_snapshot; ck snapshot $?
rm -f "$SLIST" "$SBASE"
STAGE_FAILED=0
out=$(stage_check 2>&1); rc=$?
[ "$rc" = 0 ]; ck check-lost-rc $?
case "$out" in "STAGED-MUTATED skipped (snapshot lost mid-run"*) true;; *) false;; esac; ck check-lost-line $?
case "$out" in *"cannot open"*|*"No such file"*|*"STAGED-MUTATED FAIL"*) false;; *) true;; esac; ck check-lost-no-raw $?
stage_check >/dev/null 2>&1 || stage_fail; [ "$STAGE_FAILED" = 0 ]; ck check-lost-not-red $?

# --- a real mutation is still a FAIL (the guard masks nothing)
stage_snapshot >/dev/null; ck snapshot-again $?
echo changed >"$SO/usr/lib/libone.dylib"
out=$(stage_check 2>&1); rc=$?
[ "$rc" = 1 ] && case "$out" in "STAGED-MUTATED FAIL staged tree mutated"*libone*) true;; *) false;; esac; ck check-changed-fails $?
STAGE_FAILED=0; stage_check >/dev/null 2>&1 || stage_fail; [ "$STAGE_FAILED" = 1 ]; ck mutation-sets-gate-red $?

sh -n "$GATE"; ck syntax-gate $?

if [ -z "$bad" ]; then echo "STAGE-GATE-SWEEP-TEST pass"; exit 0; fi
echo "STAGE-GATE-SWEEP-TEST fail$bad"; exit 1
