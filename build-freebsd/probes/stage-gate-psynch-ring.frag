# --- PSYNCH-RING fragment for stage-gate.sh (not wired in): paste after the PTATTACH block. Needs the darlingserver
# built with darlingserver-psynch-ring.patch; the probe sets DSERVER_PSYNCH_RING itself. Prints
# "PSYNCH-RING pass|fail expected=N got=M" and "RWLABEL pass|fail"; skipped when the run lock is busy.
[ "${STAGE_GATE_PSYNCH_RING_RUN:-1}" = 1 ] && { _p=$(sh "$HERE/probes/psynch-ring-build.sh" >/dev/null 2>&1; if [ -n "$DARLING_LOCK_HELD" ]; then DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/psynch-ring-run.sh" 2>/dev/null
	else lockf -t 0 "$DARLING_BUILD_DIR/.darling-run.lock" env DARLING_LOCK_HELD=1 DARLING_SRC_DIR="$SRCD" sh "$HERE/probes/psynch-ring-run.sh" 2>/dev/null; [ $? -eq 75 ] && echo "PSYNCH-RING skipped lock-busy"; fi | grep -a "^PSYNCH-RING\|^RWLABEL" | tail -2); echo "${_p:-PSYNCH-RING fail no-verdict}"; case "$_p" in *"fail"*|"") echo "STAGECHK FAIL psynch-ring=FAIL"; exit 1;; esac; }
