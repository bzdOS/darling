#!/bin/sh
# window-draw-probe.sh - an NSWindow filled with pure red (tests/src/window-draw-probe.c, no Chrome) on the Wayland
# backend, run like the Chrome series runs (own headless sway in a private 0700 XDG_RUNTIME_DIR, launch-dynamic under
# sudo, DARLING_GUEST_EXPOSE). After SNAP seconds: swaymsg get_tree (own socket) for a mapped view, grim snapshot,
# pixel colour at the centre of the window buffer (view origin + 200,150). Needs DARLING_SRC_DIR DARLING_OVERLAY DARLING_BUILD_DIR. Env: WAIT (default 40),
# SNAP (default 12), WL_DEBUG=1 (WAYLAND_DEBUG=1 in the guest; the log is capped at 50 MB).
# Output: WINDOW-DRAW-VERDICT pass|fail mapped=<y|n> color=<rgb> expected=255,0,0 ; rc 0 on pass.
V=WINDOW-DRAW-VERDICT; EXP=255,0,0
fail() { echo "$V fail mapped=${2:-n} color=${3:-none} expected=$EXP [$1]"; exit 1; }
SRC=$DARLING_SRC_DIR; BD=$DARLING_BUILD_DIR; OD=$DARLING_OVERLAY
[ -n "$SRC" ] && [ -n "$BD" ] && [ -n "$OD" ] || fail "env not set"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"
if [ -z "$DARLING_LOCK_HELD" ]; then
	DARLING_LOCK_HELD=1 lockf -t 0 "$BD/.darling-run.lock" sh "$0" "$@"; rc=$?
	[ $rc -eq 75 ] && fail "lock busy"; exit $rc
fi
SDK_FLAT="$BD/bundle-principal-class-test/sdk-flat"; T=window-draw-probe-macho; W="$BD/window-draw-probe"; mkdir -p "$W"
[ -d "$SDK_FLAT/usr/include" ] || fail "no flattened SDK"
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 -O1 -w \
	-I"$SRC/tests/vendor/fakesdk" -I"$SDK_FLAT/usr/include" -c "$SRC/tests/src/window-draw-probe.c" -o "$W/probe.o" &&
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$OD" -Z -o "$SRC/tests/$T" "$W/probe.o" \
	"$OD/usr/lib/AppKitExtras.dylib" \
	"$OD/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
	"$OD/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"$OD/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" "$OD/usr/lib/libobjc.A.dylib" \
	"$OD/usr/lib/libicucore.A.dylib" "$OD/usr/lib/libSystem.B.dylib" 2>"$W/link.err" || fail "build: $(tail -1 "$W/link.err")"
RUN=/tmp/wd-probe-run; rm -rf "$RUN"; mkdir -p "$RUN" && chmod 700 "$RUN"
XR=$RUN/xdg; mkdir -p "$XR" && chmod 700 "$XR"
XDG_RUNTIME_DIR=$XR WAYLAND_DISPLAY=wayland-wd WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
	sway --unsupported-gpu > "$RUN/sway.log" 2>&1 &
SWAYPID=$!
i=0; while [ ! -S "$XR/wayland-1" ] && [ $i -lt 30 ]; do sleep 1; i=$((i+1)); done
WD=$(ls "$XR" | grep -E "^wayland-[0-9]+$" | head -1)
[ -n "$WD" ] || { kill $SWAYPID; fail "sway did not start"; }
LOG=$BD/window-draw-probe.log; PNG=$BD/window-draw-probe.png; rm -f "$LOG" "$PNG"
PRE=" $(pgrep -x darlingserver | tr "\n" " ") "
( sudo -n timeout -k 5 "${WAIT:-40}" env "DARLING_SRC_DIR=$SRC" "DARLING_OVERLAY=$OD" "DARLING_BUILD_DIR=$BD" "DARLING_TEST_BINARY=$T" \
	DARLING_KEEP_LOCAL=1 DARLING_STAGING_TREES=usr/lib:System/Library/Frameworks:System/Library/PrivateFrameworks:private/etc \
	XDG_RUNTIME_DIR="$XR" WAYLAND_DISPLAY="$WD" DARLING_GUEST_EXPOSE="$XR" ${WL_DEBUG:+WAYLAND_DEBUG=1} \
	"$BD/launch-dynamic" 2>&1 | head -c 52428800 > "$LOG" & )
i=0; while [ $i -lt 40 ]; do grep -qa "WINDOW-DRAW-PROBE READY" "$LOG" 2>/dev/null && break; sleep 1; i=$((i+1)); done
sleep "${SNAP:-12}"
SS=$(ls "$XR"/sway-ipc.* 2>/dev/null | head -1)
mapped=n; TREE=$(SWAYSOCK=$SS swaymsg -t get_tree 2>/dev/null)
[ -n "$SS" ] && [ -n "$TREE" ] || mapped=unknown
echo "$TREE" | grep -q "\"shell\"" && mapped=y
XDG_RUNTIME_DIR=$XR WAYLAND_DISPLAY=$WD grim "$PNG" 2>/dev/null
color=none
if [ -s "$PNG" ]; then
	color=$(python3 - "$PNG" "$TREE" <<"PY" 2>/dev/null
import sys, json, zlib, struct
d = open(sys.argv[1], "rb").read()
pos = 8; idat = b""
while pos < len(d):
    n, t = struct.unpack(">I4s", d[pos:pos+8]); body = d[pos+8:pos+8+n]
    if t == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", body[:10])
    if t == b"IDAT": idat += body
    pos += 12 + n
bpp = {2: 3, 6: 4}[ct]; raw = zlib.decompress(idat); st = w * bpp + 1
cx, cy = w // 2, h // 2
try:
    def walk(n):
        if n.get("shell") and n.get("rect"):
            return n["rect"]
        for c in n.get("nodes", []) + n.get("floating_nodes", []):
            r = walk(c)
            if r: return r
    r = walk(json.loads(sys.argv[2]))
    if r: cx, cy = r["x"] + 200, r["y"] + 150  # the 400x300 buffer sits at the top left of the view; sway may tile the view larger
except Exception: pass
rows = []; prev = bytes(w * bpp)
for y in range(h):
    f = raw[y*st]; line = bytearray(raw[y*st+1:(y+1)*st])
    for i in range(len(line)):
        a = line[i-bpp] if i >= bpp else 0; b = prev[i]; c = prev[i-bpp] if i >= bpp else 0
        if f == 1: line[i] = (line[i] + a) & 255
        elif f == 2: line[i] = (line[i] + b) & 255
        elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
        elif f == 4:
            p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
            line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    prev = bytes(line)
    if y == cy: rows = prev
print("%d,%d,%d" % tuple(rows[cx*bpp:cx*bpp+3]))
PY
	) || color=none
fi
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo -n kill -9 "$p" 2>/dev/null;; esac; done
kill $SWAYPID 2>/dev/null; sleep 1; kill -9 $SWAYPID 2>/dev/null
[ -n "$color" ] || color=none
if [ "$mapped" = y ] && [ "$color" = "$EXP" ]; then echo "$V pass mapped=$mapped color=$color expected=$EXP"; exit 0; fi
why=""; [ "$mapped" = y ] || why="window-not-in-sway-tree"; [ "$color" = "$EXP" ] || why="$why wrong-or-no-pixel"
fail "${why# } log=$LOG png=$PNG" "$mapped" "$color"
