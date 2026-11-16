#!/bin/sh
# probe-bisect.sh - run the signal probe for every mldr x darlingserver combination (old/new), N runs each.
# usage: probe-bisect.sh [RUNS]   (PROBE_TIMEOUT seconds per run, default 240)
# env: DARLING_SRC_DIR DARLING_BUILD_DIR DARLING_OVERLAY; optional OLD_MLDR NEW_MLDR OLD_DSERVER NEW_DSERVER
#   (defaults: <build>/dserver/mldr-real/mldr.bak-premerge, the installed mldr,
#    <build>/dserver/darlingserver.pre-suspendlock, the installed darlingserver).
# The installed binaries are put back at the end (saved as *.cur next to them). One line per combination;
# exit 0 if any combination is fully OK.
: "${DARLING_SRC_DIR:?}" "${DARLING_BUILD_DIR:?}" "${DARLING_OVERLAY:?}"
export DARLING_SRC_DIR DARLING_BUILD_DIR DARLING_OVERLAY
N=${1:-3}; TMO=${PROBE_TIMEOUT:-240}   # a run takes 60-100s on a busy host; the probe default of 90s cuts it off
D=$DARLING_BUILD_DIR/dserver
ML=$D/mldr-real/mldr; DS=$D/darlingserver
OLD_MLDR=${OLD_MLDR:-$ML.bak-premerge}; OLD_DSERVER=${OLD_DSERVER:-$DS.pre-suspendlock}
cp -p "$ML" "$ML.cur" && cp -p "$DS" "$DS.cur" || exit 1
NEW_MLDR=${NEW_MLDR:-$ML.cur}; NEW_DSERVER=${NEW_DSERVER:-$DS.cur}
restore() { cp -p "$ML.cur" "$ML"; cp -p "$DS.cur" "$DS"; }
trap 'restore; exit 130' INT TERM
anyok=1
for m in old new; do
	for s in old new; do
		if [ $m = old ]; then mf=$OLD_MLDR; else mf=$NEW_MLDR; fi
		if [ $s = old ]; then sf=$OLD_DSERVER; else sf=$NEW_DSERVER; fi
		cp -p "$mf" "$ML"; cp -p "$sf" "$DS"
		sum=$(sh "$DARLING_SRC_DIR/build-freebsd/sig-reentry-probe-run.sh" "bisect-$$-$m-$s" "$N" "$TMO" | grep '^SUMMARY')
		echo "mldr=$m dserver=$s ${sum:-SUMMARY none}"
		case $sum in *"valid=$N ok=$N bad=0"*) anyok=0;; esac
	done
done
restore
exit $anyok
