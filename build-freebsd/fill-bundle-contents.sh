#!/bin/sh
# fill-bundle-contents.sh — put the vendored bundle's Contents/ over the line.
#
# Usage: sh build-freebsd/fill-bundle-contents.sh [remove]
#   remove   take the pad files out again and leave Info.plist + MacOS
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
# What it does NOT do: it does not touch src/, the harness, the vendored backend
# or the hash gate. The backend under Contents/MacOS is moved aside and put back
# byte for byte, and the script verifies both hashes before and after, so a
# mistake here cannot quietly turn the vendored-hash check into a second, less
# informative failure.
set -e

MODE="${1:-fill}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"

OD="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"
BD="${DARLING_BUILD_DIR:-/tmp}"
export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"

BUNDLE="${OD}/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/Backends/Wayland.backend"
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

[ -d "${CONTENTS}" ] || {
	printf 'no such bundle: %s\n' "${CONTENTS}" >&2
	printf 'set DARLING_OVERLAY to the overlay the probes run against\n' >&2
	exit 1
}
[ -f "${PLIST}" ] || { printf 'missing: %s\n' "${PLIST}" >&2; exit 1; }
[ -f "${MACOS_BIN}" ] || { printf 'missing: %s\n' "${MACOS_BIN}" >&2; exit 1; }

if [ "${MODE}" = "remove" ]; then
	n=0
	for i in $(seq 1 ${NPADS}); do
		f="${CONTENTS}/$(pad_name "${i}")"
		if [ -e "${f}" ]; then
			rm -f "${f}"
			n=$((n + 1))
		fi
	done
	printf 'removed %d pad file(s) from %s\n' "${n}" "${CONTENTS}"
	printf 'entries now: %s (counting . and ..)\n' \
		"$(ls -A "${CONTENTS}" | wc -l | tr -d ' ')"
	ls -f "${CONTENTS}" | sed 's/^/    /'
	exit 0
fi

# Hashes before: the two files that must survive this unchanged.
sha_plist_before="$(sha "${PLIST}")"
sha_bin_before="$(sha "${MACOS_BIN}")"

# Take the two real entries AND any earlier pads out of Contents, so the order
# below is the order the guest will see. mv within one filesystem is a rename,
# so the backend dylib is not re-read.
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

# Info.plist and MacOS first, in that order: they are what CFBundle is looking
# for, and the listing they are read out of comes back from the front.
mv "${STAGE}/Info.plist" "${PLIST}"
mv "${STAGE}/MacOS" "${MACOS}"

# Pads last. They exist only to lift the entry count over the eight-entry line,
# and the later they are created the more of them land outside the window.
i=1
while [ "${i}" -le "${NPADS}" ]; do
	printf 'x' >"${CONTENTS}/$(pad_name "${i}")"
	i=$((i + 1))
done

rm -rf "${STAGE}"

sha_plist_after="$(sha "${PLIST}")"
sha_bin_after="$(sha "${MACOS_BIN}")"
[ "${sha_plist_before}" = "${sha_plist_after}" ] || {
	printf 'Info.plist changed: %s -> %s\n' "${sha_plist_before}" "${sha_plist_after}" >&2
	exit 1
}
[ "${sha_bin_before}" = "${sha_bin_after}" ] || {
	printf 'backend changed: %s -> %s\n' "${sha_bin_before}" "${sha_bin_after}" >&2
	exit 1
}
sha_vendored="$(sha "${VENDORED}")"
[ "${sha_bin_after}" = "${sha_vendored}" ] || {
	printf 'the moved backend no longer matches the vendored copy (%s vs %s)\n' \
		"${sha_bin_after}" "${sha_vendored}" >&2
	printf "the window probe's hash gate would fail on a file this script touched\n" >&2
	exit 1
}

printf '%s\n' "${CONTENTS}"
printf 'pads: %d, created last; Info.plist and MacOS put back first\n' "${NPADS}"
printf 'listing order as the host sees it:\n'
ls -f "${CONTENTS}" | sed 's/^/    /'
n_entries="$(ls -A "${CONTENTS}" | wc -l | tr -d ' ')"
n_all=$((n_entries + 2))
printf 'entries: %s real, %s counting . and ..\n' "${n_entries}" "${n_all}"
printf 'the guest is handed the first N-7 records:\n'
ls -f "${CONTENTS}" | head -n "$((n_all - 7))" | sed 's/^/    /'
printf 'dropped from the tail: %s entr%s\n' \
	"$((n_all - (n_all - 7)))" "$([ $((n_all - (n_all - 7))) = 1 ] && echo y || echo ies)"
printf 'Info.plist is at index 2, so it is in the window for any window of 3+\n'
printf 'Info.plist sha256 %s (unchanged)\n' "${sha_plist_after}"
printf 'backend   sha256 %s (unchanged, matches tests/vendor)\n' "${sha_bin_after}"
