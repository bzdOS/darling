#!/bin/sh
# Wayland.backend (NSDisplay bundle) of the overlay.
#
# источник утрачен, бинарь Wayland.backend (30.09) — внешний артефакт, воспроизвести нельзя
#
# The Wayland.backend binary in the overlay was built on 30 Sep from sources that are not in this tree: wayland_shim.c (and the
# wayland_ifaces.c / wayland_tramp.c it was linked with) were never committed and no longer exist, so the old build (clang of the
# .m files plus the shim, ld64.lld link, install) cannot be repeated and has been removed from this script. The script now only
# checks that the prebuilt binary is in the overlay; it builds and installs nothing and never overwrites the binary.
#
# Env:
#   DARLING_OVERLAY    — darling overlay dir      (required)
set -e

OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BACKEND_DIR="${OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend/Contents"
BIN="${BACKEND_DIR}/MacOS/Wayland"

if [ ! -f "${BIN}" ]; then
	echo "WAYLAND-BACKEND FAIL: prebuilt binary missing at ${BIN}"
	exit 1
fi
echo "WAYLAND-BACKEND ok: using the prebuilt binary ${BIN}"
