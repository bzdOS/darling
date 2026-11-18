#!/bin/sh
# stage-once-test.sh - checks the STAGE-ONCE logic of stage-gate.sh on a fake overlay. No probe, no gate, no Darling start.
#   Prints one line:  STAGE-ONCE-TEST pass|fail [failed checks]      exit 0 on pass.
# Covers: md5 snapshot/check of the staged overlay files (probe binaries *-macho excluded; changed file and missing file -> FAIL line
# "STAGED-MUTATED FAIL staged tree mutated" and a failing stage_fail), the orphan report (new launch-dynamic/mldr/darlingserver pids ->
# PROBE-ORPHAN FAIL with pid and ps line; probe calls and ps are stubbed), that every edited probe script is the original except for
# the one env word (and that word is DARLING_SMOKE_REFRESH=1 when STAGE_GATE_KEEP_STAGED is unset), that series_guard is still called,
# and sh -n of the scripts.
# Env: DARLING_BUILD_DIR (default /opt/darling/build; the fake overlay lives in $DARLING_BUILD_DIR/scratch, never in the staged overlay).
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$(cd "$HERE/.." && pwd)
BD="${DARLING_BUILD_DIR:-/opt/darling/build}"; GATE="$HERE/stage-gate.sh"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
mkdir -p "$BD/scratch" || { echo "STAGE-ONCE-TEST fail no-scratch"; exit 1; }
T=$(mktemp -d "$BD/scratch/stage-once-test.XXXXXX") || { echo "STAGE-ONCE-TEST fail mktemp"; exit 1; }
trap 'rm -rf "$T"' EXIT
TMPDIR="$T"; export TMPDIR
bad=""; ck() { # name rc-of-the-condition(0 = ok)
	[ "$2" = 0 ] || bad="$bad $1"
}
PROBES="run-fsops-probe.sh run-fork-fail-probe.sh run-coretext-probe.sh font-families-probe.sh font-metrics-probe.sh font-resolve-probe.sh probes/ptattach-run.sh probes/psynch-ring-run.sh probes/catx-kvc-run.sh probes/imageio-run.sh syscall482-probe.sh port-construct-probe.sh run-dserver-survive-probe.sh vm-map-write-probe.sh"

# --- helpers from the gate
sed -n '/^# BEGIN stage-once helpers/,/^# END stage-once helpers/p' "$GATE" >"$T/helpers.sh"
[ -s "$T/helpers.sh" ]; ck helpers-extract $?
STAGE_FAILED=0; stage_fail() { STAGE_FAILED=1; }
. "$T/helpers.sh"

# --- fake overlay: files, one with a blank in its name, a probe binary
SO="$T/overlay"; mkdir -p "$SO/usr/lib" "$SO/System/Library/Frameworks/X.framework"
echo one >"$SO/usr/lib/libone.dylib"; echo two >"$SO/System/Library/Frameworks/X.framework/X"
echo three >"$SO/usr/lib/with blank.dylib"; echo bin >"$SO/fsops-probe-macho"
SLIST="$T/list"; SBASE="$T/base"; SAFTR="$T/aftr"
stage_snapshot; ck snapshot $?
[ "$(wc -l <"$SLIST" | tr -d ' ')" = 3 ]; ck list-excludes-macho $?

# probes only touch the test binary, add a file, rewrite a file with identical bytes: no mutation
echo bin2 >"$SO/fsops-probe-macho"; echo extra >"$SO/usr/lib/new.dylib"; cat "$SO/usr/lib/libone.dylib" >"$SO/usr/lib/libone.dylib.t"; mv "$SO/usr/lib/libone.dylib.t" "$SO/usr/lib/libone.dylib"
out=$(stage_check); rc=$?; [ "$rc" = 0 ] && case "$out" in "STAGED-MUTATED pass files=3") true;; *) false;; esac; ck check-pass $?

# changed file
echo changed >"$SO/usr/lib/libone.dylib"
out=$(stage_check); rc=$?; [ "$rc" = 1 ] && case "$out" in "STAGED-MUTATED FAIL staged tree mutated"*libone*) true;; *) false;; esac; ck check-changed-fails $?
STAGE_FAILED=0; stage_check >/dev/null || stage_fail; [ "$STAGE_FAILED" = 1 ]; ck mutation-sets-gate-red $?
echo one >"$SO/usr/lib/libone.dylib"; stage_check >/dev/null; ck check-restored-passes $?
# missing file (also the one with the blank)
rm "$SO/usr/lib/with blank.dylib"
out=$(stage_check); rc=$?; [ "$rc" = 1 ] && case "$out" in "STAGED-MUTATED FAIL staged tree mutated"*) true;; *) false;; esac; ck check-missing-fails $?

# the gate wires it: message, line name, rc path
grep -q 'STAGED-MUTATED FAIL staged tree mutated' "$GATE"; ck gate-has-message $?
grep -q 'stage_check || { echo "STAGECHK FAIL staged-mutated=FAIL' "$GATE"; ck gate-wires-check $?
grep -q 'STAGE_GATE_KEEP_STAGED=1; export STAGE_GATE_KEEP_STAGED' "$GATE"; ck gate-exports-keep $?
grep -q '^EXTRA_IMAGES=.*System/Library/Frameworks/AppKit.framework/Versions/C/AppKit' "$GATE"; ck gate-gates-appkit $?

# --- orphan report with a stubbed ps
PSF="$T/ps.txt"; STAGE_GATE_PS_CMD="cat '$PSF'"; STAGE_GATE_ORPHAN_WAIT=0; export STAGE_GATE_PS_CMD STAGE_GATE_ORPHAN_WAIT
echo "100 1 Tue Nov 18 10:00:00 2026 /x/launch-dynamic" >"$PSF"
echo "300 1 Tue Nov 18 10:00:00 2026 /usr/bin/something-else" >>"$PSF"
orphan_begin; ck orphan-begin $?
out=$(orphan_check FSOPS "$_OPRE"); rc=$?; [ "$rc" = 0 ] && [ -z "$out" ]; ck orphan-none-new-ok $?
echo "200 100 Tue Nov 18 10:01:00 2026 /x/mldr-real/mldr /tmp/y/probe-macho" >>"$PSF"
out=$(orphan_check FSOPS "$_OPRE"); rc=$?
[ "$rc" = 1 ] && case "$out" in *"PROBE-ORPHAN FAIL FSOPS pids=200 "*"PROBE-ORPHAN-PS 200 100 "*mldr-real/mldr*) true;; *) false;; esac; ck orphan-reported $?
STAGE_FAILED=0; orphan_end FSOPS >/dev/null; [ "$STAGE_FAILED" = 1 ]; ck orphan-sets-gate-red $?
: >"$PSF"; orphan_begin; orphan_end FSOPS >/dev/null; ck orphan-clean-ok $?
grep -q 'orphan_begin' "$GATE" && grep -q 'orphan_end "\$1"' "$GATE"; ck gate-calls-orphan $?

# --- edited probes: only the env word differs, and it is the old word with the variable unset
CMD='$([ "${STAGE_GATE_KEEP_STAGED:-}" = 1 ] && echo DARLING_KEEP_LOCAL=1 || echo DARLING_SMOKE_REFRESH=1)'
(unset STAGE_GATE_KEEP_STAGED; [ "$(eval "echo $CMD")" = "DARLING_SMOKE_REFRESH=1" ]); ck env-word-unset $?
(STAGE_GATE_KEEP_STAGED=0; [ "$(eval "echo $CMD")" = "DARLING_SMOKE_REFRESH=1" ]); ck env-word-zero $?
(STAGE_GATE_KEEP_STAGED=1; [ "$(eval "echo $CMD")" = "DARLING_KEEP_LOCAL=1" ]); ck env-word-keep $?
BASE=$(git -C "$SRC" log -1 --format=%H --grep='^build-freebsd: probes skip restage' 2>/dev/null)
if [ -n "$BASE" ]; then
	BASE=$(git -C "$SRC" rev-parse "$BASE^" 2>/dev/null)
	for f in $PROBES; do
		git -C "$SRC" show "$BASE:build-freebsd/$f" >"$T/orig" 2>/dev/null || { bad="$bad no-base:$f"; continue; }
		awk -v c="$CMD" '{ i = index($0, c); if (i) $0 = substr($0, 1, i-1) "DARLING_SMOKE_REFRESH=1" substr($0, i+length(c)); print }' "$HERE/$f" >"$T/rev"
		cmp -s "$T/orig" "$T/rev"; ck "unchanged:$f" $?
		grep -q "STAGE_GATE_KEEP_STAGED" "$HERE/$f"; ck "edited:$f" $?
		o=$(grep -c series_guard "$T/orig"); n=$(grep -c series_guard "$HERE/$f"); [ "$o" = "$n" ]; ck "series-guard:$f" $?
	done
else bad="$bad no-base-commit"; fi
grep -q 'series-guard.sh' "$HERE/run-fsops-probe.sh" && grep -q 'series_guard || exit 2' "$HERE/run-fsops-probe.sh"; ck series-guard-called $?
# the three restage scripts keep their refresh untouched
for f in cfinit-check.sh run-suspend-wake-probe.sh probes/suspend-run.sh; do
	! grep -q "STAGE_GATE_KEEP_STAGED" "$HERE/$f" && grep -q "DARLING_SMOKE_REFRESH=1" "$HERE/$f"; ck "restage-kept:$f" $?
done

# --- syntax
for f in stage-gate.sh stage-once-test.sh $PROBES; do sh -n "$HERE/$f" 2>/dev/null; ck "sh-n:$f" $?; done

if [ -z "$bad" ]; then echo "STAGE-ONCE-TEST pass"; exit 0; fi
echo "STAGE-ONCE-TEST fail$bad"; exit 1
