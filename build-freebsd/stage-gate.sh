#!/bin/sh
# stage-gate.sh - md5 gate: built vs overlay vs staged copy for mldr and every rebuilt stub image.
# usage: stage-gate.sh            Env: DARLING_BUILD_DIR DARLING_OVERLAY; STAGED_OVERLAY (default /tmp/darling-local-overlay)
# mldr is run straight from $DARLING_BUILD_DIR/dserver/mldr-real/mldr (no overlay or staged copy), so its line is
# the installed md5 only. Stub images: the list is stub-data/images.txt (written by build-stub-data.sh); the built
# file is stub-data/<basename with '.' -> '_'> where one exists. A staged copy that differs from the overlay is
# stale (launch runs with DARLING_KEEP_LOCAL=1 do not restage): it is refreshed from the overlay and re-checked.
# A built image that differs from the overlay (built, not installed) or a staged copy that stays different is a failure.
# The gated set is images.txt plus EXTRA_IMAGES (own-generator images that images.txt omits).
# Output: one "STAGECHK" line; exit 1 on any mismatch.
if [ -z "$DARLING_BUILD_DIR" ] || [ -z "$DARLING_OVERLAY" ]; then
	echo "STAGECHK FAIL missing env:$([ -z "$DARLING_BUILD_DIR" ] && printf ' DARLING_BUILD_DIR')$([ -z "$DARLING_OVERLAY" ] && printf ' DARLING_OVERLAY')"; exit 1
fi
SO="${STAGED_OVERLAY:-/tmp/darling-local-overlay}"; W="$DARLING_BUILD_DIR/stub-data"
[ -f "$DARLING_BUILD_DIR/stub-data/images.txt" ] || { echo "STAGECHK FAIL no $DARLING_BUILD_DIR/stub-data/images.txt (run build-stub-data.sh)"; exit 1; }
m() { md5 -q "$1" 2>/dev/null || echo none; }
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
	echo "STAGECHK FAIL cfinit=FAIL (CoreFoundation has no LC_ROUTINES and no ___CFInitialize __mod_init_func entry; see build-freebsd/cfinit-check.sh)"; exit 1
fi
if [ "${STAGE_GATE_CFINIT_RUN:-0}" = 1 ]; then
	cfv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/cfinit-check.sh" 2>&1 | grep -a "^CFINIT-VERDICT" | tail -1); echo "$cfv"
	case "$cfv" in "CFINIT-VERDICT pass"*) ;; *) echo "STAGECHK FAIL cfinit=FAIL"; exit 1;; esac
fi
# Guest probes that are cheap (about 3 s each) always run; STAGE_GATE_<NAME>_RUN=0 skips one. Each starts its own darlingserver, so it
# runs under the shared run lock (taken here unless the caller, e.g. chrome-series.sh, already holds it); a busy lock gives
# "<NAME>-VERDICT skipped lock-busy" and does not fail the gate (another run is using the machine).
#   FSOPS: file system probe (statfs/fstatfs, byte-range locks, fsync, mmap as a browser SQLite databases use them);
#          needs tests/fsops-probe-macho (build-fsops-probe.sh).
#   FORK:  fork/posix_spawn must return when the host cannot create the child (a stale _mach_fork_parent blocked in
#          fork_wait_for_child); needs tests/fork-fail-probe-macho (build-fork-fail-probe.sh).
gate_probe() { # name script
	_o=$(if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/$2" 2>&1
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/$2" 2>&1; [ $? -eq 75 ] && echo "$1-VERDICT skipped lock-busy"; fi | grep -a "^$1-VERDICT" | tail -1)
	[ -n "$_o" ] || _o="$1-VERDICT FAIL no-verdict"
	echo "$_o"
	case "$_o" in "$1-VERDICT pass"*|"$1-VERDICT skipped"*) ;; *) echo "STAGECHK FAIL $(echo "$1" | tr A-Z a-z)=FAIL"; exit 1;; esac
}
[ "${STAGE_GATE_FSOPS_RUN:-1}" = 1 ] && gate_probe FSOPS run-fsops-probe.sh
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
# STAGE_GATE_SURVIVE_RUN=1 also runs the darlingserver survival probe (DSERVER-SURVIVE-VERDICT: malformed requests and a client killed
# mid-call must not take the server down; about 75 s, so off by default); needs tests/dserver-survive-probe-macho.
[ "${STAGE_GATE_SURVIVE_RUN:-0}" = 1 ] && gate_probe DSERVER-SURVIVE run-dserver-survive-probe.sh
# STAGE_GATE_MLDRSOCK_RUN=1 also runs the mldr socket leak test (MLDR-SOCKET-LEAK-VERDICT pass|fail runs= leaked=; needs the fixed mldr deployed; on by default).
[ "${STAGE_GATE_MLDRSOCK_RUN:-1}" = 1 ] && gate_probe MLDR-SOCKET-LEAK mldr-socket-leak-test.sh
# SYSCALL482: thread_selfusage (BSD 482) implemented (SYSCALL482-VERDICT); default 0 until libsystem_kernel with it is deployed.
[ "${STAGE_GATE_SYSCALL482_RUN:-0}" = 1 ] && gate_probe SYSCALL482 syscall482-probe.sh
# PORT-CONSTRUCT: mach_port_construct always gets a reply (PORT-CONSTRUCT-VERDICT); default 0 (diagnostic for the Chrome pre-window stall).
[ "${STAGE_GATE_PORTCONSTRUCT_RUN:-0}" = 1 ] && gate_probe PORT-CONSTRUCT port-construct-probe.sh
# STAGE_GATE_SUSPEND_RUN=1 also runs the suspend/resume lost-wakeup probe (SUSPEND-WAKE-VERDICT pass|fail n= stalls= starved=: a thread is
# suspended/resumed while it waits on a Mach semaphore; fail = a wakeup never arrives, even with the suspender stopped; up to
# ~2 min, so off by default); needs tests/suspend-wake-probe-macho (build-suspend-wake-probe.sh).
[ "${STAGE_GATE_SUSPEND_RUN:-0}" = 1 ] && gate_probe SUSPEND-WAKE run-suspend-wake-probe.sh
# STAGE_GATE_WINDOW_RUN=1 also runs the window draw probe (WINDOW-DRAW-VERDICT line: an NSWindow with a red background must
# map in an own headless sway and show that colour in a grim snapshot; own sway and darlingserver, off by default).
if [ "${STAGE_GATE_WINDOW_RUN:-0}" = 1 ]; then
	wdv=$(DARLING_SRC_DIR="$SRCD" sh "$HERE/window-draw-probe.sh" 2>&1 | grep -a "^WINDOW-DRAW-VERDICT"); echo "$wdv"
	# one line per window (plain, modal alert): both must pass
	[ "$(echo "$wdv" | grep -c "^WINDOW-DRAW-VERDICT pass")" = 2 ] && ! echo "$wdv" | grep -q "verdict fail\|VERDICT fail" || { echo "STAGECHK FAIL window=FAIL"; exit 1; }
fi
# CoreGraphics must carry the NULL-safe CGColor accessors (cg-color-nullsafe.py); overlay and staged copy are both checked.
cgp=ok; CGR=System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
for f in "$DARLING_OVERLAY/$CGR" "$SO/$CGR"; do
	[ -f "$f" ] || continue
	python3 "$HERE/cg-color-nullsafe.py" --check "$f" >/dev/null 2>&1 || cgp=FAIL
done
[ -f "$DARLING_OVERLAY/$CGR" ] || cgp=FAIL
if [ "$cgp" = FAIL ]; then
	echo "STAGECHK FAIL cgpatch=FAIL (CoreGraphics CGColor accessors not NULL-safe; run build-freebsd/cg-color-nullsafe.py on the overlay and staged copy)"; exit 1
fi
# No script here may stop processes by name (pkill/killall hit other users' processes): comments are ignored,
# this file is skipped (it names the words). NOKILL_DIR overrides the scanned directory (for tests).
nk=ok; NKD="${NOKILL_DIR:-$HERE}"
for f in "$NKD"/*.sh; do
	[ "$(basename "$f")" = stage-gate.sh ] && continue
	if sed 's/#.*//' "$f" 2>/dev/null | grep -Eq '(^|[^[:alnum:]_-])(pkill|killall)([^[:alnum:]_-]|$)'; then nk=FAIL; nkf="$nkf $(basename "$f")"; fi
done
if [ "$nk" = FAIL ]; then
	echo "STAGECHK FAIL nokill=FAIL (name-based process kill in:${nkf}; stop own processes by pid)"; exit 1
fi
# Disk guards must exist and be overridable (static check only: disk-guard-selftest.sh runs chrome-series.sh, which runs this gate).
dg=ok
grep -q SERIES_DISK_MAX "$HERE/chrome-series.sh" && grep -q SERIES_DISK_STOP "$HERE/chrome-sr-batch.sh" \
	&& [ -f "$HERE/disk-guard-selftest.sh" ] && sh -n "$HERE/disk-guard-selftest.sh" || dg=FAIL
if [ "$dg" = FAIL ]; then
	echo "STAGECHK FAIL diskguard=FAIL (SERIES_DISK_MAX/SERIES_DISK_STOP guard or disk-guard-selftest.sh missing)"; exit 1
fi
# Relink reproducibility: static flag check only (relink-repro-check.sh without --static does two relinks; opt-in).
sh "$HERE/relink-repro-check.sh" --static || { echo "STAGECHK FAIL relinkrepro=FAIL"; exit 1; }
# One tree, one final build, nothing grows: keep-one-check.sh is read-only (never deletes). Reported as keepone=<verdict>;
# it fails the gate only with STAGE_GATE_KEEPONE=1 (line: KEEP-ONE verdict=pass|fail ...; --list names the candidates).
ko=skip
if [ -f "$HERE/keep-one-check.sh" ]; then
	kol=$(sh "$HERE/keep-one-check.sh" 2>/dev/null | tail -1)
	case "$kol" in "KEEP-ONE verdict=pass"*) ko=pass;; *) ko=fail;; esac
	if [ "$ko" = fail ] && [ "${STAGE_GATE_KEEPONE:-0}" = 1 ]; then
		echo "STAGECHK FAIL keepone=FAIL ($kol)"; exit 1
	fi
fi
if [ -z "$bad" ] && [ "$ML" != none ]; then
	echo "STAGECHK ok cfinit=ok cgpatch=$cgp nokill=$nk diskguard=$dg keepone=$ko images=$n mldr=$ML restaged=[${fixed# }] key:$sel"; exit 0
fi
echo "STAGECHK FAIL images=$n mldr=$ML mismatches:${bad:- none}"; exit 1
