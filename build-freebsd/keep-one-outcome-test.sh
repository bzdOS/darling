#!/bin/sh
# keep-one-outcome-test.sh - static/logic checks, no gate and no probe is run:
#  1. CWD does not change the candidate set: keep-one-check.sh --list from the tree root, from build-freebsd/ and from
#     build/ answers the same paths (22.11: from build-freebsd/ 12 keep-listed build/ dirs turned into experiment dirs,
#     because the unquoted $KEEP_DIRS patterns expanded against the probe sources in that directory).
#  2. --apply-check deletes nothing and answers ok (rc 0) / refused (rc 4).
#  3. stage-gate.sh tells the three keepone outcomes apart, with keep-one-check.sh --apply-check stubbed:
#     pass / fail-apply-ok (total<=max, candidates removable) / FAIL (over=1 or --apply-check refused).
#   Prints one line:  KEEPONE-OUTCOME-TEST pass|fail [failed checks]      exit 0 on pass.
# Env: DARLING_BUILD_DIR or KEEPONE_ROOT for the CWD check (default: skip it when neither is set).
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$(cd "$HERE/.." && pwd)
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
bad=""; ck() { [ "$2" = 0 ] || { bad="$bad $1"; echo "FAIL $1"; }; }
T=$(mktemp -d /tmp/keepone-outcome.XXXXXX) || { echo "KEEPONE-OUTCOME-TEST fail mktemp"; exit 1; }
trap 'rm -rf "$T"' EXIT

# --- 1. CWD independence (only against a real tree)
ROOT="${KEEPONE_ROOT:-}"; [ -z "$ROOT" ] && [ -n "${DARLING_BUILD_DIR:-}" ] && ROOT=$(dirname "$DARLING_BUILD_DIR")
if [ -n "$ROOT" ] && [ -d "$ROOT/build" ]; then
	(cd "$ROOT" && sh "$HERE/keep-one-check.sh" --list) >"$T/root" 2>/dev/null; ck cwd-root $?
	(cd "$HERE" && sh ./keep-one-check.sh --list) >"$T/bfb" 2>/dev/null; ck cwd-bfb $?
	(cd "$ROOT/build" && sh "$HERE/keep-one-check.sh" --list) >"$T/build" 2>/dev/null; ck cwd-build-dir $?
	cmp -s "$T/root" "$T/bfb"; ck cwd-same-bfb $?
	cmp -s "$T/root" "$T/build"; ck cwd-same-build-dir $?
	# the phantom dirs of 22.11 were build/ dirs on the keep-list, so they must never appear as candidates
	grep -q '/build/font-metrics-probe$' "$T/bfb"; [ $? != 0 ]; ck cwd-no-phantom $?
else
	echo "note: no KEEPONE_ROOT/DARLING_BUILD_DIR, CWD check skipped"
fi

# --- 2. --apply-check on a fake tree: removable candidate -> ok, and nothing is deleted
R="$T/fake"; mkdir -p "$R/build" "$R/backup"
echo x >"$R/build/stale.log"; touch -t 202001010000 "$R/build/stale.log"
out=$(KEEPONE_ROOT="$R" sh "$HERE/keep-one-check.sh" --apply-check 2>/dev/null); rc=$?
[ $rc = 0 ] && case "$out" in "KEEP-ONE apply-check ok paths=1") true;; *) false;; esac; ck applycheck-ok $?
[ -f "$R/build/stale.log" ]; ck applycheck-deletes-nothing $?

# --- 3. the three gate outcomes, --apply-check stubbed
GATE_DIR="$HERE"
sed -n '/# START_KEEPONE_VERDICT/,/# END_KEEPONE_VERDICT/p' "$GATE_DIR/stage-gate.sh" >"$T/ko.sh"
[ -s "$T/ko.sh" ]; ck ko-extract $?
printf '#!/bin/sh\nexit "${AC_RC:-0}"\n' >"$T/keep-one-check.sh"
HERE="$T"
. "$T/ko.sh"
ko_case() { # want verdict-line check-rc apply-rc
	ko=skip
	AC_RC="$4"; export AC_RC
	ko_out "$2" "$3" >"$T/line" 2>&1
	[ "$ko" = "$1" ] || echo "  got ko=$ko want $1"
	[ "$ko" = "$1" ]; ck "ko-$1" $?
}
ko_case pass          "KEEP-ONE verdict=pass series=a,b stale_logs=0 candidate_bytes=0 total=12.9G max=14G over=0" 0 9
ko_case fail-apply-ok "KEEP-ONE verdict=fail series=a,b stale_logs=5 candidate_bytes=65160576 total=12.9G max=14G over=0" 1 0
ko_case FAIL          "KEEP-ONE verdict=fail series=a,b stale_logs=1 experiment_dirs=2 total=12.9G max=14G over=0" 1 4
ko_case FAIL          "KEEP-ONE verdict=fail series=a,b stale_logs=1 total=14.5G max=14G over=1" 1 0
# a refused cleanup is red and prints the FAIL line
AC_RC=4; export AC_RC; ko=skip
ko_out "KEEP-ONE verdict=fail series=a,b stale_logs=1 total=1.0G max=14G over=0" 1 >"$T/line" 2>&1
grep -q 'STAGECHK FAIL keepone=FAIL' "$T/line"; ck refused-is-red $?
# usage error: rc 2, no "verdict=" word in the line -> not a verdict fail, red, and not printed as a verdict fail
ko=skip; AC_RC=0; export AC_RC
ko_out "KEEP-ONE usage error: KEEPONE_ROOT or DARLING_BUILD_DIR required" 2 >"$T/line" 2>&1
[ "$ko" = FAIL ]; ck ko-usage-red $?
grep -q 'keep-one-check usage error, not a verdict' "$T/line"; ck ko-usage-not-verdict $?

# sh -n of both scripts
sh -n "$GATE_DIR/keep-one-check.sh"; ck syntax-keepone $?
sh -n "$GATE_DIR/stage-gate.sh"; ck syntax-gate $?

[ -z "$bad" ] && { echo "KEEPONE-OUTCOME-TEST pass"; exit 0; }
echo "KEEPONE-OUTCOME-TEST fail$bad"; exit 1
