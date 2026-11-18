#!/bin/sh
# static checks (nothing is run): chrome-series.sh asks stage-gate for keep-one explicitly, not the full gate;
# stage-gate.sh names the mode full only for RUN_ALL=1, else quick.
D=$(dirname "$0"); v=pass
grep -q 'STAGE_GATE_KEEPONE=1 sh .*stage-gate.sh' "$D/chrome-series.sh" || { v=fail; echo "chrome-series: no explicit KEEPONE=1"; }
grep -q 'STAGE_GATE_FULL' "$D/chrome-series.sh" && { v=fail; echo "chrome-series: uses STAGE_GATE_FULL"; }
grep -q '_md=quick; \[ "${STAGE_GATE_RUN_ALL:-0}" = 1 \] && _md=full' "$D/stage-gate.sh" || { v=fail; echo "stage-gate: mode rule missing"; }
grep -q 'GATE mode=partial' "$D/stage-gate.sh" && { v=fail; echo "stage-gate: partial left"; }
echo "KEEPONE-GATEMODE-TEST $v"
[ $v = pass ]
