#!/bin/sh
# build-stub-data.sh - rebuild every stub framework / stand-in dylib whose data is exported
# as code, then fail the build if any generated stub still does it.
#
#   1. check-data-as-code.py classifies the installed overlay (ObjC classes, constants and
#      block objects bound through non-lazy pointers by the consumers) and writes the data
#      names to $BUILD/stub-data/data-names.txt (its exit status is the old state's).
#   2. rebuild-stub-data.py re-emits each stub image that has such a name: data as zero
#      filled objects, ObjC classes as real root classes, a name a real provider defines
#      as data is not shadowed. The old image is kept as <image>.pre-dataascode.
#   3. the gate runs again and must pass (set -e).
#
# Images with their own generator keep it: suppl.dylib and cg-supl.dylib
# (build-suppl-stubs.sh), the OpenDirectory stand-in (build-opendirectory-stub.sh).
# Set CHROME_APP so the Chrome framework is a consumer (rule c of the check).
#
# Usage: DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=... [CHROME_APP=...] \
#        sh build-freebsd/build-stub-data.sh [--install]
set -e
# apply build-freebsd/patches/series to the submodule trees (idempotent; fails the build on conflict)
sh "$(dirname "$0")/apply-patches.sh" || exit 1
SRC="${DARLING_SRC_DIR:?}"; OV="${DARLING_OVERLAY:?}"; BD="${DARLING_BUILD_DIR:?}"
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin"; export PATH
W="$BD/stub-data"
mkdir -p "$W"
CHK="$SRC/build-freebsd/check-data-as-code.py"
python3 "$CHK" --overlay "$OV" --emit-data "$W/data-names.txt" > "$W/check-before.txt" 2>&1 || true
tail -1 "$W/check-before.txt"
python3 "$CHK" --overlay "$OV" --list-stubs > "$W/stubs.txt"
# images that have their own generator are left to it, and the real system libraries
# under usr/lib/system are never rewritten
grep -v -e '/usr/lib/suppl.dylib$' -e '/usr/lib/cg-supl.dylib$' \
        -e '/OpenDirectory.framework/' -e '/usr/lib/system/' "$W/stubs.txt" > "$W/images.txt"
# shellcheck disable=SC2046
python3 "$SRC/build-freebsd/rebuild-stub-data.py" --overlay "$OV" --work "$W" \
    --data-list "$W/data-names.txt" "$@" $(cat "$W/images.txt")
# the gate: fails the build and lists the offenders when any stub still exports data as code
# (only meaningful once the rebuilt images are installed)
case " $* " in
*" --install "*) python3 "$CHK" --overlay "$OV" ;;
*) echo "build-stub-data: rebuilt images are in $W only (no --install): gate not run" ;;
esac
