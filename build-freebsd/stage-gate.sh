#!/bin/sh
# stage-gate.sh - md5 gate: built vs overlay vs staged copy for mldr and every rebuilt stub image.
# usage: stage-gate.sh            Env: DARLING_BUILD_DIR DARLING_OVERLAY; STAGED_OVERLAY (default /tmp/darling-local-overlay)
# mldr is run straight from $DARLING_BUILD_DIR/dserver/mldr-real/mldr (no overlay or staged copy), so its line is
# the installed md5 only. Stub images: the list is stub-data/images.txt (written by build-stub-data.sh); the built
# file is stub-data/<basename with '.' -> '_'> where one exists. A staged copy that differs from the overlay is
# stale (launch runs with DARLING_KEEP_LOCAL=1 do not restage): it is refreshed from the overlay and re-checked.
# A built image that differs from the overlay (built, not installed) or a staged copy that stays different is a failure.
# The gated set is images.txt plus EXTRA_IMAGES (own-generator images that images.txt omits).
# Output: one "STAGECHK" line; exit 1 on any mismatch. STAGE_GATE_RUN_ALL=1 runs every line and exits 1 at the end if any was red.
# STAGE_GATE_FULL=1: every line runs (RUN_ALL=1) and the optional probes are on: SYSCALL482, PORTCONSTRUCT, SUSPEND (wake) and
# SUSPEND_RWLOCK/MUTEX/CV. ImageIO stays off. The last line is always "GATE mode=full|quick skipped=<n> rc=<n>": full only when RUN_ALL=1
# (every line runs), else quick; skipped counts the lines that did; rc is the real exit code of the gate (the same value the gate exits with).
if [ "${STAGE_GATE_FULL:-0}" = 1 ]; then
	: "${STAGE_GATE_KEEPONE:=1}"; export STAGE_GATE_KEEPONE
	STAGE_GATE_RUN_ALL=1; STAGE_GATE_SYSCALL482_RUN=1; STAGE_GATE_PORTCONSTRUCT_RUN=1; STAGE_GATE_SUSPEND_RUN=1
	STAGE_GATE_SUSPEND_RWLOCK_RUN=1; STAGE_GATE_SUSPEND_MUTEX_RUN=1; STAGE_GATE_SUSPEND_CV_RUN=1
	export STAGE_GATE_RUN_ALL STAGE_GATE_SYSCALL482_RUN STAGE_GATE_PORTCONSTRUCT_RUN STAGE_GATE_SUSPEND_RUN \
		STAGE_GATE_SUSPEND_RWLOCK_RUN STAGE_GATE_SUSPEND_MUTEX_RUN STAGE_GATE_SUSPEND_CV_RUN
fi
if [ -z "$STAGE_GATE_INNER" ]; then
	# Every line reaches the terminal as soon as its probe is done (tee); the copy in $_gt only feeds the skipped count.
	_gt=$(mktemp "${TMPDIR:-/tmp}/stage-gate.XXXXXX") || { echo "GATE mode=quick skipped=0 rc=1"; exit 1; }
	_rcf=$(mktemp "${TMPDIR:-/tmp}/stage-gate-rc.XXXXXX") || { rm -f "$_gt"; echo "GATE mode=quick skipped=0 rc=1"; exit 1; }
	{ STAGE_GATE_INNER=1 sh "$0" 2>&1; echo $? >"$_rcf"; } | tee "$_gt"
	_rc=$(cat "$_rcf"); [ -n "$_rc" ] || _rc=1
	_sk=$(grep -c "skipped" "$_gt"); rm -f "$_gt" "$_rcf"
	_md=quick; [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ] && _md=full
	echo "GATE mode=$_md skipped=$_sk rc=$_rc"; exit "$_rc"
fi
# DARLING_BUILD_DIR / DARLING_OVERLAY unset: take them from the node env file (KEY=VALUE lines, read as data, never sourced).
# DARLING_ENV_FILE names the file; an absent file changes nothing.
_envf="${DARLING_ENV_FILE:-/opt/darling/build/darling.env}"
if { [ -z "$DARLING_BUILD_DIR" ] || [ -z "$DARLING_OVERLAY" ]; } && [ -f "$_envf" ]; then
	while IFS= read -r _l; do
		_v=${_l#*=}; _v=${_v#\"}; _v=${_v%\"}; _v=${_v#\'}; _v=${_v%\'}
		case "$_l" in
		DARLING_BUILD_DIR=*) [ -n "$DARLING_BUILD_DIR" ] || DARLING_BUILD_DIR=$_v;;
		DARLING_OVERLAY=*) [ -n "$DARLING_OVERLAY" ] || DARLING_OVERLAY=$_v;;
		esac
	done <"$_envf"
	export DARLING_BUILD_DIR DARLING_OVERLAY
fi
if [ -z "$DARLING_BUILD_DIR" ] || [ -z "$DARLING_OVERLAY" ]; then
	echo "STAGECHK FAIL missing env:$([ -z "$DARLING_BUILD_DIR" ] && printf ' DARLING_BUILD_DIR')$([ -z "$DARLING_OVERLAY" ] && printf ' DARLING_OVERLAY')"; exit 1
fi
# STAGE_GATE_RUN_ALL=1: run to the end. Every line still runs and prints; a failing line sets RED=1 instead of exiting,
# and the gate exits 1 at the end if any line was red. Without it the first red line exits 1 as before.
RED=0
stage_fail() { RED=1; [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ] || exit 1; }
SO="${STAGED_OVERLAY:-/tmp/darling-local-overlay}"; W="$DARLING_BUILD_DIR/stub-data"
[ -f "$DARLING_BUILD_DIR/stub-data/images.txt" ] || { echo "STAGECHK FAIL no $DARLING_BUILD_DIR/stub-data/images.txt (run build-stub-data.sh)"; exit 1; }
m() { md5 -q "$1" 2>/dev/null || echo none; }
# BEGIN stage-once helpers (stage-once-test.sh extracts this block; keep it free of other gate state)
# STAGE-ONCE: the staged overlay is staged by one launch (see the staging run below), then every shared-staging probe runs with
# STAGE_GATE_KEEP_STAGED=1 (DARLING_KEEP_LOCAL, no refresh). stage_snapshot lists the staged regular files that came from the overlay
# (test binaries *-macho excluded) and their md5; stage_check compares them again after the last shared probe.
stage_hash() { ( cd "$SO" && tr '\n' '\0' <"$SLIST" | xargs -0 md5 -r 2>&1 ) | sort; }
stage_snapshot() {
	( cd "$SO" && find . -type f ! -name '*-macho' | sort ) >"$SLIST" || return 1
	[ -s "$SLIST" ] || return 1
	stage_hash >"$SBASE"
}
stage_check() {
	stage_hash >"$SAFTR"
	if cmp -s "$SBASE" "$SAFTR"; then echo "STAGED-MUTATED pass files=$(wc -l <"$SLIST" | tr -d ' ')"; return 0; fi
	echo "STAGED-MUTATED FAIL staged tree mutated: $(comm -13 "$SBASE" "$SAFTR" | wc -l | tr -d ' ') file(s) changed or missing, first: $(comm -13 "$SBASE" "$SAFTR" | head -3 | cut -c1-140 | tr '\n' ';')"
	return 1
}
# Orphans: after a probe, no process of it may remain. New pids (not in the snapshot taken before the probe) of launch-dynamic,
# mldr or darlingserver are reported with pid and full ps line; nothing is stopped. STAGE_GATE_PS_CMD overrides the ps command (tests);
# STAGE_GATE_ORPHAN_WAIT = seconds to let a probe's own teardown finish before it counts (default 10).
orphan_pids() { eval "${STAGE_GATE_PS_CMD:-ps -axo pid=,ppid=,lstart=,args=}" 2>/dev/null | grep -E 'launch-dynamic|mldr-real/mldr|darlingserver' | grep -v 'grep -E'; }
orphan_begin() { _OPRE=$(mktemp "${TMPDIR:-/tmp}/stage-gate-pre.XXXXXX") || return 1; orphan_pids | awk '{print $1}' | sort >"$_OPRE"; }
orphan_check() { # name prefile
	_oi=0
	while :; do
		_onew=$(orphan_pids | awk -v pre="$(tr '\n' ' ' <"$2")" 'BEGIN{n=split(pre,a," ");for(i=1;i<=n;i++)p[a[i]]=1} !($1 in p)')
		[ -z "$_onew" ] && return 0
		[ "$_oi" -ge "${STAGE_GATE_ORPHAN_WAIT:-10}" ] && break
		sleep 1; _oi=$((_oi+1))
	done
	echo "PROBE-ORPHAN FAIL $1 pids=$(echo "$_onew" | awk '{printf "%s ", $1}')(still alive after the probe; reported only, nothing stopped)"
	echo "$_onew" | while IFS= read -r _ol; do echo "PROBE-ORPHAN-PS $_ol"; done
	return 1
}
orphan_end() { # name
	orphan_check "$1" "$_OPRE"; _or=$?; rm -f "$_OPRE"
	[ "$_or" = 0 ] || { echo "STAGECHK FAIL $(echo "$1" | tr A-Z a-z)=orphan"; stage_fail; }
}
# END stage-once helpers
SLIST=$(mktemp "${TMPDIR:-/tmp}/stage-gate-list.XXXXXX"); SBASE=$(mktemp "${TMPDIR:-/tmp}/stage-gate-base.XXXXXX"); SAFTR=$(mktemp "${TMPDIR:-/tmp}/stage-gate-aftr.XXXXXX")
trap 'rm -f "$SLIST" "$SBASE" "$SAFTR"' EXIT
ML=$(m "$DARLING_BUILD_DIR/dserver/mldr-real/mldr")
# images with their own generator that build-stub-data.sh leaves out of images.txt but whose md5 is still gated
# (SystemConfigurationExtras.dylib: build-sc-stubs.sh; it was counted until it was excluded from images.txt)
EXTRA_IMAGES="usr/lib/SystemConfigurationExtras.dylib"
n=0; bad=""; fixed=""; sel=""
while read -r p; do
	[ -n "$p" ] || continue
	r=${p#"$DARLING_OVERLAY"/}; base=$(basename "$p" | tr . _)
	o=$(m "$p"); s=$(m "$SO/$r"); b=$o
	[ -f "$W/$base" ] && b=$(m "$W/$base")
	if [ "$o" != "$s" ] && [ "$b" = "$o" ] && [ -f "$p" ]; then
		/usr/local/bin/sudo -n cp -p "$p" "$SO/$r" 2>/dev/null && { fixed="$fixed $r"; s=$(m "$SO/$r"); }
	fi
	n=$((n+1))
	if [ "$o" != "$s" ] || [ "$o" != "$b" ]; then bad="$bad $r(built=$b overlay=$o staged=$s)"; fi
	case "$r" in */Security.framework/*|usr/lib/AppKitExtras.dylib|usr/lib/FoundationExtras.dylib|usr/lib/IOKitExtras.dylib)
		sel="$sel $(basename "$r")=$o" ;; esac
done <<EOT
$(cat "$W/images.txt"; for x in $EXTRA_IMAGES; do grep -qx "$DARLING_OVERLAY/$x" "$W/images.txt" || echo "$DARLING_OVERLAY/$x"; done)
EOT
# CoreFoundation must carry its initializer (LC_ROUTINES or a __mod_init_func entry for ___CFInitialize): cfinit-check.sh --static.
# STAGE_GATE_CFINIT_RUN=1 also runs the guest probe (CFINIT-VERDICT line; it starts its own darlingserver, so it is off by default).
HERE=$(cd "$(dirname "$0")" && pwd); SRCD="${DARLING_SRC_DIR:-$(cd "$HERE/.." && pwd)}"
if ! DARLING_SRC_DIR="$SRCD" sh "$HERE/cfinit-check.sh" --static >/dev/null 2>&1; then
	echo "STAGECHK FAIL cfinit=FAIL (CoreFoundation has no LC_ROUTINES and no ___CFInitialize __mod_init_func entry; see build-freebsd/cfinit-check.sh)"; stage_fail
fi
# CFINIT guest probe: OWN RESTAGE (keeps its DARLING_SMOKE_REFRESH): its meaning is "a fresh staging of the overlay yields a
# CoreFoundation that initializes"; a reused staged tree would hide a staging fault. It runs in the restage group at the end of the gate.
cfinit_run_group() {
if [ "${STAGE_GATE_CFINIT_RUN:-0}" = 1 ]; then
	orphan_begin
	cfv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/cfinit-check.sh" 2>&1 | grep -a "^CFINIT-VERDICT" | tail -1); echo "$cfv"
	case "$cfv" in "CFINIT-VERDICT pass"*) ;; *) echo "STAGECHK FAIL cfinit=FAIL"; stage_fail;; esac
	orphan_end CFINIT
fi
}
# Guest probes that are cheap (about 3 s each) always run; STAGE_GATE_<NAME>_RUN=0 skips one. Each starts its own darlingserver, so it
# runs under the shared run lock (taken here unless the caller, e.g. chrome-series.sh, already holds it); a busy lock gives
# "<NAME>-VERDICT skipped lock-busy" and does not fail the gate (another run is using the machine).
#   FSOPS: file system probe (statfs/fstatfs, byte-range locks, fsync, mmap as a browser SQLite databases use them);
#          needs tests/fsops-probe-macho (build-fsops-probe.sh).
#   FORK:  fork/posix_spawn must return when the host cannot create the child (a stale _mach_fork_parent blocked in
#          fork_wait_for_child); needs tests/fork-fail-probe-macho (build-fork-fail-probe.sh).
gate_probe() { # name script
	orphan_begin
	_o=$(if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/$2" 2>&1
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/$2" 2>&1; [ $? -eq 75 ] && echo "$1-VERDICT skipped lock-busy"; fi | grep -a "^$1-VERDICT" | tail -1)
	[ -n "$_o" ] || _o="$1-VERDICT FAIL no-verdict"
	echo "$_o"
	case "$_o" in "$1-VERDICT pass"*|"$1-VERDICT skipped"*) ;; *) echo "STAGECHK FAIL $(echo "$1" | tr A-Z a-z)=FAIL"; stage_fail;; esac
	orphan_end "$1"
}
# STAGING RUN (STAGE_GATE_ONCE=0 turns STAGE-ONCE off: every probe restages as before). The FSOPS probe is the one launch that stages:
# REFRESH (no STAGE_GATE_KEEP_STAGED yet) plus DARLING_KEEP_LOCAL=1 so the tree survives it, with the superset of the trees the probes
# derive. Then the file list and md5 of the staged overlay files are taken and STAGE_GATE_KEEP_STAGED=1 is exported: the probes edited
# for it (fsops, fork, coretext, font-*, ptattach, psynch-ring, catx-kvc, imageio, syscall482, port-construct, dserver-survive,
# vmmap-write, and mldr-socket-leak through the fork probe) reuse the tree. No staging run (FSOPS off, lock busy, fail) = old behaviour.
STAGED_ONCE=0
if [ "${STAGE_GATE_ONCE:-1}" = 1 ] && [ "${STAGE_GATE_FSOPS_RUN:-1}" = 1 ]; then
	_pe_save="${PROBE_ENV-}"
	PROBE_ENV="$_pe_save DARLING_KEEP_LOCAL=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc"; export PROBE_ENV
	gate_probe FSOPS run-fsops-probe.sh
	PROBE_ENV="$_pe_save"; [ -n "$PROBE_ENV" ] || unset PROBE_ENV
	case "$_o" in
	"FSOPS-VERDICT pass"*)
		if stage_snapshot; then
			STAGED_ONCE=1; STAGE_GATE_KEEP_STAGED=1; export STAGE_GATE_KEEP_STAGED
			echo "STAGE-ONCE staged files=$(wc -l <"$SLIST" | tr -d ' ')"
		else echo "STAGE-ONCE FAIL no staged overlay files to hash in $SO"; echo "STAGECHK FAIL stage-once=FAIL"; stage_fail; fi;;
	*) echo "STAGE-ONCE skipped (staging run gave no pass; probes restage)";;
	esac
else
	[ "${STAGE_GATE_FSOPS_RUN:-1}" = 1 ] && gate_probe FSOPS run-fsops-probe.sh
fi
[ "${STAGE_GATE_FORK_RUN:-1}" = 1 ] && gate_probe FORK run-fork-fail-probe.sh
# VMMAP-WRITE: write-after-map/protect/remap probe (needs tests/vm-map-write-probe-macho, build-vm-map-write-probe.sh); off (=0) until the darlingserver adjacent-entry fix is deployed.
# remap copy=FALSE still KERN_FAILURE in dtape_vm_map_remap (VMMAP-WRITE off by default)
[ "${STAGE_GATE_VMMAP_RUN:-0}" = 1 ] && gate_probe VMMAP-WRITE vm-map-write-probe.sh
#   CORETEXT: the CoreText Create/Copy functions answer objects, never NULL (both CoreText and CoreTextExtras images);
#          needs tests/coretext-probe-macho and tests/coretext-extras-probe-macho (build-coretext-probe.sh).
[ "${STAGE_GATE_CORETEXT_RUN:-1}" = 1 ] && gate_probe CORETEXT run-coretext-probe.sh
#   FONT-METRICS: NSFont/CoreText metric accessors (ascender, descender, capHeight, leading, xHeight, pointSize, CTFontGet*/Copy*) answer
#          sane values, no NULL, no exception (font-metrics-probe.sh builds tests/font-metrics-probe-macho itself).
[ "${STAGE_GATE_FONTMETRICS_RUN:-1}" = 1 ] && gate_probe FONT-METRICS font-metrics-probe.sh
#   FONT-RESOLVE: with darling-extras.dylib injected (as Chrome runs) CTFontCreateWithName("Helvetica",12) and +[NSFont systemFontOfSize:12] are
#          not nil and the flat lookup of CTFontCreateWithName is not answered by darling-extras (font-resolve-probe.sh builds its own binary).
[ "${STAGE_GATE_FONTRESOLVE_RUN:-1}" = 1 ] && gate_probe FONT-RESOLVE font-resolve-probe.sh
#   CATX-KEYS: the key of +[CATransaction setValue:forKey:] that Chrome passes (kCATransactionDisableActions) is non-null in every image that exports it
#          (static check of the overlay, no run; catx-keys-probe.sh). A NULL key raises "not key value coding compliant for the key (null)".
[ "${STAGE_GATE_CATXKEYS_RUN:-1}" = 1 ] && { _c=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/catx-keys-probe.sh" 2>&1 | grep -a "^CATX-KEYS" | tail -1); echo "${_c:-CATX-KEYS fail no-verdict}"; case "$_c" in "CATX-KEYS pass"*) ;; *) echo "STAGECHK FAIL catx-keys=FAIL"; stage_fail;; esac; }
#   NULL-CONST: no string constant (NSString * / CFStringRef) that the Chrome framework or its helpers import is a zero object in the image that provides it
[ "${STAGE_GATE_NULLCONST_RUN:-1}" = 1 ] && { _n=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/null-const-probe.sh" 2>&1 | grep -a "^NULL-CONST" | tail -1); echo "${_n:-NULL-CONST fail no-verdict}"; case "$_n" in "NULL-CONST pass"*) ;; *) echo "STAGECHK FAIL null-const=FAIL"; stage_fail;; esac; }
#   PTATTACH: concurrent PT_ATTACH on one pid must not fail (probes/ptattach-build.sh builds tests/ptattach-probe-macho; probes/ptattach-run.sh
#          prints PTATTACH pass|fail calls= fail=; needs the darlingserver with the memory-access serialization). Runs under the shared run lock.
[ "${STAGE_GATE_PTATTACH_RUN:-1}" = 1 ] && { orphan_begin; _p=$(sh "$HERE/probes/ptattach-build.sh" >/dev/null 2>&1; if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/ptattach-run.sh" 2>/dev/null
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/ptattach-run.sh" 2>/dev/null; [ $? -eq 75 ] && echo "PTATTACH skipped lock-busy"; fi | grep -a "^PTATTACH" | tail -1); echo "${_p:-PTATTACH fail no-verdict}"; case "$_p" in "PTATTACH pass"*|"PTATTACH skipped"*) ;; *) echo "STAGECHK FAIL ptattach=FAIL"; stage_fail;; esac; orphan_end PTATTACH; }
# PSYNCH-RING + RWLABEL: psynch ring probe (probes/psynch-ring-run.sh), needs the server built with darlingserver-psynch-ring.patch.
[ "${STAGE_GATE_PSYNCH_RING_RUN:-1}" = 1 ] && { orphan_begin; _p=$(sh "$HERE/probes/psynch-ring-build.sh" >/dev/null 2>&1; if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/psynch-ring-run.sh" 2>/dev/null
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/psynch-ring-run.sh" 2>/dev/null; [ $? -eq 75 ] && echo "PSYNCH-RING skipped lock-busy"; fi | grep -a "^PSYNCH-RING\|^RWLABEL" | tail -2); echo "${_p:-PSYNCH-RING fail no-verdict}"; case "$_p" in *"fail"*|"") echo "STAGECHK FAIL psynch-ring=FAIL"; stage_fail;; esac; orphan_end PSYNCH-RING; }
# CATX-KVC: +[CATransaction setValue:forKey:kCATransactionDisableActions] must not raise (probes/catx-kvc-run.sh).
[ "${STAGE_GATE_CATXKVC_RUN:-1}" = 1 ] && { orphan_begin; _k=$(sh "$HERE/probes/catx-kvc-build.sh" >/dev/null 2>&1; if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/catx-kvc-run.sh" 2>/dev/null
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/catx-kvc-run.sh" 2>/dev/null; [ $? -eq 75 ] && echo "CATX-KVC-PROOF skipped lock-busy"; fi | grep -a "^CATX-KVC-PROOF" | tail -1); echo "${_k:-CATX-KVC-PROOF no-verdict}"; case "$_k" in *"throws=n"*|"CATX-KVC-PROOF skipped"*) ;; *) echo "STAGECHK FAIL catx-kvc=FAIL"; stage_fail;; esac; orphan_end CATX-KVC; }
#   FONT-FAMILIES: [[NSDisplay currentDisplay] allFontFamilyNames] answers a non-empty set and does not raise "only defined for abstract class";
#          needs the AppKit Wayland backend with cocotron-wayland-font-families deployed, so default 0 until then (font-families-probe.sh).
[ "${STAGE_GATE_FONTFAMILIES_RUN:-1}" = 1 ] && gate_probe FONT-FAMILIES font-families-probe.sh
# IMAGEIO-DECODE + IMGREP-NULL (STAGE_GATE_IMAGEIO_RUN=1, default off until the real ImageIO is shipped): the real ImageIO decodes a generated 8-bit PNG (size matches) and gives a NULL source for garbage;
#          +[NSBitmapImageRep canInitWithData:] on garbage answers NO without crashing (probes/imageio-run.sh, runs under the shared run lock).
[ "${STAGE_GATE_IMAGEIO_RUN:-0}" = 1 ] && { orphan_begin; _i=$(if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/imageio-run.sh" 2>/dev/null
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/imageio-run.sh" 2>/dev/null; [ $? -eq 75 ] && echo "IMAGEIO-DECODE skipped lock-busy"; fi | grep -a "^IMAGEIO-DECODE\|^IMGREP-NULL")
	[ -n "$_i" ] || _i="IMAGEIO-DECODE FAIL no-verdict"; echo "$_i"
	case "$_i" in *"IMAGEIO-DECODE pass"*|*"IMAGEIO-DECODE skipped"*) ;; *) echo "STAGECHK FAIL imageio-decode=FAIL"; stage_fail;; esac
	case "$_i" in *"IMGREP-NULL pass"*|*"IMAGEIO-DECODE skipped"*) ;; *) echo "STAGECHK FAIL imgrep-null=FAIL"; stage_fail;; esac; orphan_end IMAGEIO; }
# STAGE_GATE_SURVIVE_RUN=1 also runs the darlingserver survival probe (DSERVER-SURVIVE-VERDICT: malformed requests and a client killed
# mid-call must not take the server down; about 75 s, so off by default); needs tests/dserver-survive-probe-macho.
[ "${STAGE_GATE_SURVIVE_RUN:-0}" = 1 ] && gate_probe DSERVER-SURVIVE run-dserver-survive-probe.sh
# STAGE_GATE_MLDRSOCK_RUN=1 also runs the mldr socket leak test (MLDR-SOCKET-LEAK-VERDICT pass|fail runs= leaked=; needs the fixed mldr deployed; on by default).
[ "${STAGE_GATE_MLDRSOCK_RUN:-1}" = 1 ] && gate_probe MLDR-SOCKET-LEAK mldr-socket-leak-test.sh
# SYSCALL482: thread_selfusage (BSD 482) implemented (SYSCALL482-VERDICT); default 0 until libsystem_kernel with it is deployed.
if [ "${STAGE_GATE_SYSCALL482_RUN:-0}" = 1 ]; then gate_probe SYSCALL482 syscall482-probe.sh
elif [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ]; then echo "SYSCALL482-VERDICT skipped (RUN=0)"; fi
# PORT-CONSTRUCT: mach_port_construct always gets a reply (PORT-CONSTRUCT-VERDICT); default 0 (diagnostic for the Chrome pre-window stall).
if [ "${STAGE_GATE_PORTCONSTRUCT_RUN:-0}" = 1 ]; then gate_probe PORT-CONSTRUCT port-construct-probe.sh
elif [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ]; then echo "PORT-CONSTRUCT-VERDICT skipped (RUN=0)"; fi
# RESTAGE GROUP: SUSPEND-WAKE and SUSPEND-RWLOCK/MUTEX/CV keep their own restage (DARLING_SMOKE_REFRESH, STAGE_GATE_KEEP_STAGED is
# unset for them) and run after the staged-tree md5 check, at the end of the gate. Reason (conservative, not proven necessary): they
# suspend guest threads and stop the guest by timeout, a run can end with files half written, and the probes are long and rare, so a
# fresh staging per run is cheaper than doubting the shared tree. A refresh wipes the shared tree when they finish.
restage_group() {
# STAGE_GATE_SUSPEND_RUN=1 also runs the suspend/resume lost-wakeup probe (SUSPEND-WAKE-VERDICT pass|fail n= stalls= starved=: a thread is
# suspended/resumed while it waits on a Mach semaphore; fail = a wakeup never arrives, even with the suspender stopped; up to
# ~2 min, so off by default); needs tests/suspend-wake-probe-macho (build-suspend-wake-probe.sh).
if [ "${STAGE_GATE_SUSPEND_RUN:-0}" = 1 ]; then gate_probe SUSPEND-WAKE run-suspend-wake-probe.sh
elif [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ]; then echo "SUSPEND-WAKE-VERDICT skipped (RUN=0)"; fi
# SUSPEND-RWLOCK / SUSPEND-MUTEX / SUSPEND-CV (STAGE_GATE_SUSPEND_<RWLOCK|MUTEX|CV>_RUN=1, default 0): a thread thread_suspend()ed while blocked
#   in psynch_rw_rdlock / psynch_mutexwait / psynch_cvwait gets EINTR from the server; the line is
#   "SUSPEND-<KIND> confirmed= iters= eintr=E retry_same_seq=R invariant_broken=I hung=y|n pass=y|n" (probes/suspend-run.sh, SR_KIND=rwlock|mutex|cv).
#   pass: eintr>0 and invariant_broken=0 and hung=n and (rwlock, mutex: every interrupted call repeated, retry_same_seq==eintr;
#   cv: libpthread makes EINTR a spurious wakeup, no repeat required). Needs the darlingserver with the psynch ring and the BSD EINTR sign fix.
#   Skipped when the run lock is busy; RUN-INVALID is not a failure.
gate_suspend() {
	orphan_begin
	_s=$(SR_KIND="$1" sh "$HERE/probes/suspend-build.sh" >/dev/null 2>&1; if [ -n "$DARLING_LOCK_HELD" ]; then SR_KIND="$1" DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/suspend-run.sh" 2>/dev/null
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 SR_KIND="$1" DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/suspend-run.sh" 2>/dev/null; [ $? -eq 75 ] && echo "$2 skipped lock-busy"; fi | grep -a "^$2 \|^RUN-INVALID" | tail -1)
	echo "${_s:-$2 fail no-verdict}"
	case "$_s" in *" pass=y"|*"skipped"*|RUN-INVALID*) ;; *) echo "STAGECHK FAIL $3=FAIL"; stage_fail;; esac
	orphan_end "$2"
}
if [ "${STAGE_GATE_SUSPEND_RWLOCK_RUN:-0}" = 1 ]; then gate_suspend rwlock SUSPEND-RWLOCK suspend-rwlock
elif [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ]; then echo "SUSPEND-RWLOCK skipped (RUN=0)"; fi
if [ "${STAGE_GATE_SUSPEND_MUTEX_RUN:-0}" = 1 ]; then gate_suspend mutex SUSPEND-MUTEX suspend-mutex
elif [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ]; then echo "SUSPEND-MUTEX skipped (RUN=0)"; fi
if [ "${STAGE_GATE_SUSPEND_CV_RUN:-0}" = 1 ]; then gate_suspend cv SUSPEND-CV suspend-cv
elif [ "${STAGE_GATE_RUN_ALL:-0}" = 1 ]; then echo "SUSPEND-CV skipped (RUN=0)"; fi
}
# STAGE_GATE_WINDOW_RUN=1 also runs the window draw probe (WINDOW-DRAW-VERDICT line: an NSWindow with a red background must
# map in an own headless sway and show that colour in a grim snapshot; own sway and darlingserver, off by default).
if [ "${STAGE_GATE_WINDOW_RUN:-0}" = 1 ]; then
	wdv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/window-draw-probe.sh" 2>&1 | grep -a "^WINDOW-DRAW-VERDICT"); echo "$wdv"
	# one line per window (plain, modal alert): both must pass
	[ "$(echo "$wdv" | grep -c "^WINDOW-DRAW-VERDICT pass")" = 2 ] && ! echo "$wdv" | grep -q "verdict fail\|VERDICT fail" || { echo "STAGECHK FAIL window=FAIL"; stage_fail; }
fi
# STAGED-MUTATED: after the last shared-staging probe the staged overlay files must hash as they did right after staging.
if [ "$STAGED_ONCE" = 1 ]; then
	stage_check || { echo "STAGECHK FAIL staged-mutated=FAIL (staged tree mutated)"; stage_fail; }
fi
# CoreGraphics must carry the NULL-safe CGColor accessors (cg-color-nullsafe.py); overlay and staged copy are both checked.
cgp=ok; CGR=System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
for f in "$DARLING_OVERLAY/$CGR" "$SO/$CGR"; do
	[ -f "$f" ] || continue
	python3 "$HERE/cg-color-nullsafe.py" --check "$f" >/dev/null 2>&1 || cgp=FAIL
done
[ -f "$DARLING_OVERLAY/$CGR" ] || cgp=FAIL
if [ "$cgp" = FAIL ]; then
	echo "STAGECHK FAIL cgpatch=FAIL (CoreGraphics CGColor accessors not NULL-safe; run build-freebsd/cg-color-nullsafe.py on the overlay and staged copy)"; stage_fail
fi
# No script here may stop processes by name (pkill/killall hit other users' processes): comments are ignored,
# this file is skipped (it names the words). NOKILL_DIR overrides the scanned directory (for tests).
nk=ok; NKD="${NOKILL_DIR:-$HERE}"
for f in "$NKD"/*.sh; do
	[ "$(basename "$f")" = stage-gate.sh ] && continue
	if sed 's/#.*//' "$f" 2>/dev/null | grep -Eq '(^|[^[:alnum:]_-])(pkill|killall)([^[:alnum:]_-]|$)'; then nk=FAIL; nkf="$nkf $(basename "$f")"; fi
done
if [ "$nk" = FAIL ]; then
	echo "STAGECHK FAIL nokill=FAIL (name-based process kill in:${nkf}; stop own processes by pid)"; stage_fail
fi
# Disk guards must exist and be overridable (static check only: disk-guard-selftest.sh runs chrome-series.sh, which runs this gate).
dg=ok
grep -q SERIES_DISK_MAX "$HERE/chrome-series.sh" && grep -q SERIES_DISK_STOP "$HERE/chrome-sr-batch.sh" \
	&& [ -f "$HERE/disk-guard-selftest.sh" ] && sh -n "$HERE/disk-guard-selftest.sh" || dg=FAIL
if [ "$dg" = FAIL ]; then
	echo "STAGECHK FAIL diskguard=FAIL (SERIES_DISK_MAX/SERIES_DISK_STOP guard or disk-guard-selftest.sh missing)"; stage_fail
fi
# Relink reproducibility: static flag check only (relink-repro-check.sh without --static does two relinks; opt-in).
sh "$HERE/relink-repro-check.sh" --static || { echo "STAGECHK FAIL relinkrepro=FAIL"; stage_fail; }
# STAGED-DRIFT: no .c may be compiled from a staged copy (it would shadow the tree the series patches). Table: fixed strings that
# name a staged source directory (literal and shell variable spellings); scanned in build-*.sh, comments ignored. pass files=0 when none.
sd=0; sdf=""
for f in "$HERE"/build-*.sh; do
	[ -f "$f" ] || continue
	c=$(sed 's/#.*//' "$f" | awk 'BEGIN{n=split("pthread-staged/|${PTHREAD_STAGED}/|$PTHREAD_STAGED/",K,"|")} {for(i=1;i<=n;i++) if(index($0,K[i])>0 && $0 ~ /\.c([^[:alnum:]_]|$)/){c++; break}} END{print c+0}')
	[ "$c" -gt 0 ] && { sd=$((sd+c)); sdf="$sdf $(basename "$f")"; }
done
if [ "$sd" = 0 ]; then echo "STAGED-DRIFT pass files=0"; else echo "STAGED-DRIFT fail files=$sd in:$sdf"; echo "STAGECHK FAIL staged-drift=FAIL"; stage_fail; fi
# One tree, one final build, nothing grows: keep-one-check.sh is read-only (never deletes). Reported as keepone=<verdict>;
# it fails the gate only with STAGE_GATE_KEEPONE=1 (default 1 in the full gate, STAGE_GATE_FULL=1; set STAGE_GATE_KEEPONE=0 to opt out; line: KEEP-ONE verdict=pass|fail ...; --list names the candidates).
ko=skip
if [ -f "$HERE/keep-one-check.sh" ]; then
	kol=$(sh "$HERE/keep-one-check.sh" 2>/dev/null | tail -1)
	case "$kol" in "KEEP-ONE verdict=pass"*) ko=pass;; *) ko=fail;; esac
	if [ "$ko" = fail ] && [ "${STAGE_GATE_KEEPONE:-0}" = 1 ]; then
		echo "STAGECHK FAIL keepone=FAIL ($kol)"; stage_fail
	fi
fi
# Restage group (own staging each, see restage_group): the shared staging is over, the probes get their default refresh back.
unset STAGE_GATE_KEEP_STAGED
cfinit_run_group
restage_group
if [ -z "$bad" ] && [ "$ML" != none ] && [ "$RED" = 0 ]; then
	echo "STAGECHK ok cfinit=ok cgpatch=$cgp nokill=$nk diskguard=$dg keepone=$ko images=$n mldr=$ML restaged=[${fixed# }] key:$sel"; exit 0
fi
if [ "$RED" = 1 ] && [ -z "$bad" ] && [ "$ML" != none ]; then
	echo "STAGECHK FAIL red lines above (STAGE_GATE_RUN_ALL=1); images=$n mldr=$ML"
else
	echo "STAGECHK FAIL images=$n mldr=$ML mismatches:${bad:- none}"
fi
exit 1
