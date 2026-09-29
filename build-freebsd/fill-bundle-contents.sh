#!/bin/sh
# fill-bundle-contents.sh — put the vendored bundle's Contents/ over the line.
#
# Usage:
#   sh build-freebsd/fill-bundle-contents.sh                  # = contents
#   sh build-freebsd/fill-bundle-contents.sh backends         # = Backends/
#   sh build-freebsd/fill-bundle-contents.sh remove [contents|backends]
#
#   contents   the default: pad the vendored bundle's Contents/
#   backends   pad Resources/Backends/ so the backend is discoverable
#   remove     take the pad files out again
#
# Env: DARLING_OVERLAY (the overlay, i.e. the guest's root), DARLING_BUILD_DIR
#      (scratch; only used for the short staging directory while Info.plist and
#      MacOS are out of the way). Neither is a build product: this edits the
#      overlay in place, because the overlay is the only place the guest can
#      see a fixture from.
#
# Why this exists
# ---------------
# `SLOT-344.md` §1 measured the guest's directory enumeration to the entry:
# `getdirentries` answers EINVAL below EIGHT entries, and at eight and above it
# answers records — but only the first N-7 of them, counted from `.`. A
# directory with fewer than eight entries therefore looks empty to the guest,
# and one that puts what we need at the end looks empty at that end too.
#
# `PRINCIPAL-CLASS.md` §3(a) is the wall that follows from it: CFBundle finds
# Info.plist by *listing* Contents/ (`_CFIterateDirectory`,
# CFBundle_InfoPlist.c:490), and a listing that comes back empty leaves
# `infoDictionary` an empty dummy, so `-[NSBundle principalClass]` returns nil
# and the window is never built.
#
# Contents/ holds four entries counting `.` and `..` — Info.plist, MacOS — which
# is four, i.e. below the line. That is the entire reason the wall is standing,
# and it is a property of a directory we ship, not of a bug in src/.
#
# The layout, and why the pads come LAST
# -------------------------------------
# The window is the HEAD of the listing. `SLOT-344.md` §1 read the ten fixture
# directories as "the entries from index 7 onward", but every one of those runs
# printed its first record as d_name="." — index 0. The records come back from
# the front and the last few are dropped; nothing is ever returned from the
# back. So what matters is that Info.plist and MacOS are among the FIRST
# entries, and the pads have to be created after them, not before.
#
# Creation order is the listing order on the filesystem this runs on (UFS keeps
# small directories linearly, in insertion order), so the layout is
#
#     index  0  1        2             3       4 .. 10
#            .  ..  Info.plist      MacOS   pad-01..pad-07
#
# eleven entries, of which the first four are the ones the guest is handed.
# Info.plist sits at index 2, so it survives any window of three records or
# more — and every successful call measured so far has returned at least four
# (`t8` with eight entries is the smallest that answers at all, and it returns
# one). More pads do not buy anything, and fewer would risk a two-record
# window; seven keeps the total above the eight-entry line with room to spare.
#
# The pad names are ten characters and are neither "Info.plist" nor
# "Info-macos.plist" in any case, so `_CFIterateDirectory` matches them against
# nothing (CFBundle_InfoPlist.c:490-517 only compares full-length, anchored,
# case-insensitive), and the bundle's executable, resources and hashes are
# untouched. The pad bodies are one byte; nothing reads them.
#
# Backends/, the same wall one directory up
# ------------------------------------------
# `Resources/Backends/` holds one entry counting `.` and `..` — Wayland.backend
# — which is below the eight-entry line, so `+[NSDisplay init]`'s discovery
#
#     [appKitBundle pathsForResourcesOfType: @"backend" inDirectory: @"Backends"]
#
# (src/external/cocotron/AppKit/NSDisplay.m:60-66) gets an empty array back,
# never enters its instantiation loop, and raises "Failed to connect to a
# window server. Available backends are: <CFArray>{count = 0}".
#
# The same head-window rule applies, and the geometry happens to be already
# right: Wayland.backend is the first real entry, so it is at index 2, and the
# seven pads go after it.
#
# Two things differ from Contents/ and both are load-bearing:
#
#   * The pad names must not end in `.backend`. The type is taken as
#     everything after the LAST dot (`_CFBundleSplitFileName`,
#     CFBundle_Resources.c:543-575) and filed under the query-table key
#     `*.backend`, so a pad called `pad.backend` would be handed to
#     NSDisplay as a real backend and `[NSBundle bundleWithPath:]` would be
#     asked about a directory that is not a bundle. `pad-NN.txt` is type
#     `txt` and matches nothing.
#   * The directory being padded holds the bundle itself, not two files, so
#     what gets moved aside and put back is `Wayland.backend` whole, and the
#     hashes checked afterwards are the ones inside it.
#
# `fill backends` does that; `fill contents` is the original operation. Both
# are idempotent and both are undone by `remove [contents|backends]`.
#
# The framework root, `Resources/Backends/` one level further out
# -----------------------------------------------------------
# Two walls meet in the framework ROOT, and both are reached before
# `Backends/` is ever opened.
#
# **The count.** `_CFBundleCreateQueryTableAtPath` determines the bundle
# layout by iterating the framework root looking for `Resources`,
# `Contents` or `Support Files`, matching `DT_DIR` or `DT_LNK`
# (CFBundle_Resources.c:255-266). `AppKit.framework/` holds three entries —
# `AppKit`, `Resources`, `Versions` — five counting `.` and `..`, which is
# below the line, so the listing is empty, `foundResources` is never set and
# the layout is never determined. Measured: 0 entries returned by readdir.
#
# **The symlink.** `Resources` is a symlink to `Versions/Current/Resources`,
# and the staged copy of the tree carries no symlinks at all
# (`launch-dynamic-smoke.c`: `find . -type f` emits regular files only), so
# in the guest `AppKit.framework/Resources/Backends` does not exist and
# `opendir` answers ENOENT. `launch-dynamic-smoke.c` now carries the links
# across; this script does not touch that, and `remove framework-root` will
# not bring a symlink back.
#
# The pads go AFTER the existing entries and nothing is moved: `AppKit`,
# `Resources` and `Versions` have to stay at indices 0, 1 and 2, because they
# are the three names the layout scan is looking for and the window is a
# prefix of the listing. The pad names must not collide with `Resources`,
# `Contents` or `Support Files` — a pad called `Contents.txt` is fine, a pad
# called `Contents` would be found by the detector and change the layout it
# was added to measure.
#
# One caution, recorded because it is not obvious: the window this relies on
# is the head of the listing, and how many records the guest drops from the
# tail is NOT pinned (WORKAROUND-344.md §9: 7, 7, 6, 6, 3 and 4 across six
# measured directories). Twelve entries and a seven-record drop leaves five
# records, which covers indices 0..4 and so covers all three names. That is
# arithmetic, not a measurement, and the probe run that follows prints the
# names it actually got — so if it is short, that printout is the correction
# and not a surprise.

# What it does NOT do: it does not touch src/, the harness, the vendored backend
# or the hash gate. The backend under Contents/MacOS is moved aside and put back
# byte for byte, and the script verifies both hashes before and after, so a
# mistake here cannot quietly turn the vendored-hash check into a second, less
# informative failure.
set -e

MODE="${1:-contents}"

case "${MODE}" in
fill | contents | backends | framework-root | remove) ;;
*)
	printf 'usage: %s [contents|backends|framework-root|remove [what]]\n' "$0" >&2
	exit 2
	;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"

OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BD="${DARLING_BUILD_DIR:-/tmp}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

BACKENDS="${OD}/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends"
FWROOT="${OD}/System/Library/Frameworks/AppKit.framework"
BUNDLE="${BACKENDS}/Wayland.backend"
CONTENTS="${BUNDLE}/Contents"
PLIST="${CONTENTS}/Info.plist"
MACOS="${CONTENTS}/MacOS"
MACOS_BIN="${MACOS}/Wayland"
VENDORED="${SRC}/tests/vendor/wayland-backend/Wayland"

NPADS=7
pad_name() { printf 'pad-%02d.txt' "$1"; }

sha() {
	if command -v sha256 >/dev/null 2>&1; then
		sha256 -q "$1"
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

# The two checks every mode makes before it touches anything: the plist is
# there and readable, and the backend is byte-identical to the committed copy.
# A run that starts from a broken overlay should say so here, not four lines
# later in a form that looks like this script's fault.
check_bundle_intact() {
	[ -d "${BUNDLE}" ] || {
		printf 'no such backend bundle: %s\n' "${BUNDLE}" >&2
		printf 'set DARLING_OVERLAY to the overlay the probes run against\n' >&2
		exit 1
	}
	[ -f "${PLIST}" ] || { printf 'missing: %s\n' "${PLIST}" >&2; exit 1; }
	[ -f "${MACOS_BIN}" ] || { printf 'missing: %s\n' "${MACOS_BIN}" >&2; exit 1; }
	sha_vendored="$(sha "${VENDORED}")"
	sha_bin_now="$(sha "${MACOS_BIN}")"
	[ "${sha_bin_now}" = "${sha_vendored}" ] || {
		printf 'the overlay backend does not match tests/vendor (%s vs %s)\n' \
			"${sha_bin_now}" "${sha_vendored}" >&2
		printf 'fix that first: this script moves that file and would report a\n' >&2
		printf 'hash mismatch that is not its own\n' >&2
		exit 1
	}
}

sha_vendored=""
sha_bin_now=""
check_bundle_intact

if [ "${MODE}" = "remove" ]; then
	TARGET="${2:-contents}"
	case "${TARGET}" in
	backends) DIR="${BACKENDS}" ;;
	framework-root) DIR="${FWROOT}" ;;
	contents) DIR="${CONTENTS}" ;;
	*)
		printf 'remove: second argument must be contents, backends or framework-root\n' >&2
		exit 2
		;;
	esac
	n=0
	for i in $(seq 1 ${NPADS}); do
		f="${DIR}/$(pad_name "${i}")"
		if [ -e "${f}" ]; then
			rm -f "${f}"
			n=$((n + 1))
		fi
	done
	printf 'removed %d pad file(s) from %s\n' "${n}" "${DIR}"
	printf 'entries now: %s (counting . and ..)\n' \
		"$(ls -A "${DIR}" | wc -l | tr -d ' ')"
	ls -f "${DIR}" | sed 's/^/    /'
	exit 0
fi

if [ "${MODE}" = "backends" ]; then
	DIR="${BACKENDS}"
	# Clear out the bundle and any earlier pads, put the bundle back so it is
	# the first real entry again, and only then create the pads. The order of
	# these three operations is the whole point: the bundle has to be at index
	# 2 when the listing is taken, and the pads have to come after it.
	STAGE="${BD}/fill-bundle-contents.$$"
	rm -rf "${STAGE}"
	mkdir -p "${STAGE}"
	mv "${BUNDLE}" "${STAGE}/Wayland.backend"
	for i in $(seq 1 ${NPADS}); do
		p="${DIR}/$(pad_name "${i}")"
		if [ -e "${p}" ]; then
			mv "${p}" "${STAGE}/$(pad_name "${i}")"
		fi
	done

	mv "${STAGE}/Wayland.backend" "${BUNDLE}"

	i=1
	while [ "${i}" -le "${NPADS}" ]; do
		printf 'x' >"${DIR}/$(pad_name "${i}")"
		i=$((i + 1))
	done

	rm -rf "${STAGE}"
elif [ "${MODE}" = "framework-root" ]; then
	DIR="${FWROOT}"
	WANTED='AppKit / Resources / Versions'
	# Nothing is moved and nothing is removed: the three real entries have to
	# keep their places, and the pads are created after them. Only a pad from
	# an earlier run is cleared, so a second run is a no-op.
	for i in $(seq 1 ${NPADS}); do
		p="${DIR}/$(pad_name "${i}")"
		if [ -e "${p}" ]; then
			rm -f "${p}"
		fi
	done

	i=1
	while [ "${i}" -le "${NPADS}" ]; do
		printf 'x' >"${DIR}/$(pad_name "${i}")"
		i=$((i + 1))
	done
else
	DIR="${CONTENTS}"
	# Take the two real entries AND any earlier pads out of Contents, so the
	# order below is the order the guest will see. mv within one filesystem is
	# a rename, so the backend dylib is not re-read.
	STAGE="${BD}/fill-bundle-contents.$$"
	rm -rf "${STAGE}"
	mkdir -p "${STAGE}"
	mv "${PLIST}" "${STAGE}/Info.plist"
	mv "${MACOS}" "${STAGE}/MacOS"
	for i in $(seq 1 ${NPADS}); do
		p="${CONTENTS}/$(pad_name "${i}")"
		if [ -e "${p}" ]; then
			mv "${p}" "${STAGE}/$(pad_name "${i}")"
		fi
	done

	# Info.plist and MacOS first, in that order: they are what CFBundle is
	# looking for, and the listing they are read out of comes back from the
	# front.
	mv "${STAGE}/Info.plist" "${PLIST}"
	mv "${STAGE}/MacOS" "${MACOS}"

	# Pads last. They exist only to lift the entry count over the eight-entry
	# line, and the later they are created the more of them land outside the
	# window.
	i=1
	while [ "${i}" -le "${NPADS}" ]; do
		printf 'x' >"${CONTENTS}/$(pad_name "${i}")"
		i=$((i + 1))
	done

	rm -rf "${STAGE}"
fi

sha_plist_after="$(sha "${PLIST}")"
sha_bin_after="$(sha "${MACOS_BIN}")"
[ "${sha_bin_now}" = "${sha_bin_after}" ] || {
	printf 'backend changed: %s -> %s\n' "${sha_bin_now}" "${sha_bin_after}" >&2
	printf 'this script moved it; that is a bug in the script, not in the run\n' >&2
	exit 1
}
sha_vendored="$(sha "${VENDORED}")"
[ "${sha_bin_after}" = "${sha_vendored}" ] || {
	printf 'the moved backend no longer matches the vendored copy (%s vs %s)\n' \
		"${sha_bin_after}" "${sha_vendored}" >&2
	printf "the window probe's hash gate would fail on a file this script touched\n" >&2
	exit 1
}

printf '%s\n' "${DIR}"
WANTED='Info.plist'
if [ "${MODE}" = "backends" ]; then
	printf 'pads: %d, created after Wayland.backend, which was put back at index 2\n' \
		"${NPADS}"
	WANTED='Wayland.backend'
elif [ "${MODE}" = "framework-root" ]; then
	printf 'pads: %d, created after the existing entries; nothing moved\n' "${NPADS}"
	WANTED='the framework root'"'"'s own three entries'
else
	printf 'pads: %d, created last; Info.plist and MacOS put back first\n' "${NPADS}"
fi
printf 'listing order as the host sees it:\n'
ls -f "${DIR}" | sed 's/^/    /'
n_entries="$(ls -A "${DIR}" | wc -l | tr -d ' ')"
n_all=$((n_entries + 2))
printf 'entries: %s real, %s counting . and ..\n' "${n_entries}" "${n_all}"
printf 'the guest is handed a PREFIX of this; the length of that prefix is not\n'
printf 'pinned (WORKAROUND-344.md §9), so read the names the probe prints.\n'
printf 'first %s records, which is what a seven-record drop would leave here:\n' \
	"$((n_all - 7))"
ls -f "${DIR}" | head -n "$((n_all - 7))" | sed 's/^/    /'
if [ "${MODE}" = "framework-root" ]; then
	# The three names the layout scan looks for are the three real entries at
	# the front, so print their real indices rather than asserting a position.
	printf 'the layout scan wants Resources, Contents or Support Files; here are\n'
	printf 'the real entries that precede the first pad, in listing order:\n'
	ls -f "${DIR}" | sed -n '3,8p' | while read -r n; do
		case "${n}" in
		pad-*) break ;;
		*) printf '    %s\n' "${n}" ;;
		esac
	done
else
	printf '%s is at index 2, so it is in the window for any window of 3+\n' \
		"${WANTED}"
fi
printf 'Info.plist sha256 %s\n' "${sha_plist_after}"
printf 'backend   sha256 %s (matches tests/vendor)\n' "${sha_bin_after}"
