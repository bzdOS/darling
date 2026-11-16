#!/bin/sh
# chrome-log-triage.sh LOG - one summary line per Chrome run log.
#   TRIAGE <tag> rc=<wrapper rc> wl_connect=0/1 surface=0/1 toplevel=0/1 last=<...> crash=<image+off sym>
# last  : last non-traplog line, plus the dominant guest syscall numbers of the final 3000 lines
# crash : first FATAL signal block: signal, rip image+offset (from the in-log fatal stack), top frames
# Exit 0 on success (log readable).
L="${1:?usage: chrome-log-triage.sh LOG}"
[ -r "$L" ] || { echo "TRIAGE $L unreadable" >&2; exit 2; }
tag=$(basename "$L" .log); tag=${tag##*-}
rc=$(sed -n 's/^WRAPPER_RC=//p' "$L" | tail -1)
wl=0; grep -aq 'wl_display_connect returned 0x[0-9a-f][0-9a-f]*' "$L" && wl=1
sf=0; grep -aE 'create_surface|wl_surface[^_]|WaylandWindow|WaylandSurface' "$L" | grep -avq 'registry.global\|wl_surface_interface' && sf=1
tl=0; grep -aE 'xdg_toplevel|get_toplevel|get_xdg_surface|xdg_surface' "$L" | grep -avq 'registry.global' && tl=1
lastl=$(grep -av '^\[traplog\]' "$L" | grep -av '^WRAPPER_\|^SNAP\|^$' | tail -1 | cut -c1-90)
sys=$(tail -3000 "$L" | sed -n 's/.*SIGILL ENTER tid=[0-9]* p=[^ ]* b=\([0-9]*\).*/\1/p' | sort | uniq -c | sort -rn | head -3 | awk '{printf "%s%s(x%s)", (NR>1?",":""), $2, $1}')
fl=$(grep -an 'FATAL signal' "$L" | head -1 | cut -d: -f1)
if [ -n "$fl" ]; then
  sig=$(sed -n "${fl}p" "$L" | sed 's/.*FATAL signal \([0-9]*\).*/\1/')
  top=$(sed -n "$((fl+1)),$((fl+40))p" "$L" | grep -a 'fatal stack\|^\[darling-mldr\]   at ' | sed -n '2,4p' \
        | sed 's/^\[darling-mldr\]   at //; s/ rip=.*//' | tr ' ' '_' | tr '\n' '>' )
  crash="sig${sig}:${top%>}"
else
  crash="none(no-fatal-handler-output;killed-before-handler)"
fi
echo "TRIAGE $tag rc=${rc:-?} wl_connect=$wl surface=$sf toplevel=$tl last=<${lastl}|sys:${sys}> crash=$crash"
exit 0
