#!/bin/sh
# Cross-compile real Mach-O test binaries with a real toolchain (clang
# targeting x86_64-apple-macos + LLVM's ld64.lld, linking against the actual
# libSystem.B.dylib), as opposed to the hand-assembled ones gen-hello-*.py
# produces byte by byte.
#
# Produces:
#   tests/hello-cctools-macho — trivial puts() program (tests/hello-cctools.c)
#   tests/sqlite3-real-macho  — the real, unmodified upstream SQLite CLI
#                               (src/external/sqlite/{sqlite3,shell}.c)
#   tests/hello-objc-macho    — minimal Objective-C program exercising the
#                               real libobjc.A.dylib runtime (tests/hello-objc.m)
#   tests/hello-cf-macho      — minimal CoreFoundation program (CFString
#                               create/uppercase/length) against the real
#                               CoreFoundation.framework (tests/hello-cf.c)
#   tests/hello-foundation-macho — NSObject/NSString/NSMutableArray program
#                               (tests/hello-foundation.m) against a
#                               Foundation.dylib built from the real,
#                               unmodified darling-foundation submodule
#                               sources (233 of 235 — see the exclusions
#                               noted below). Also INSTALLS that
#                               Foundation.dylib into
#                               $DARLING_OVERLAY/System/Library/Frameworks/,
#                               a persistent side effect (unlike the other
#                               targets, which only touch tests/) — needed
#                               so launch-dynamic-smoke.c's own per-run
#                               System/Library/Frameworks staging step picks
#                               it up without further changes.
#   tests/hello-appkit-macho    — minimal AppKit program (NSApplication
#                               +sharedApplication) against the AppKit.dylib
#                               and stub frameworks built by build-gui.sh.
#
# Usage: sh build-freebsd/build-real-macho-tests.sh
# Run on the FreeBSD dev VM (185) — needs clang and ld64.lld (both ship with
# the llvm* pkg already required for other builds in this repo).
#
# Environment:
#   DARLING_BUILD_DIR — scratch build dir        (default: /tmp/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
#   DARLING_OVERLAY    — darling overlay dir      (required)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/real-macho"
OVERLAY="${DARLING_OVERLAY:?set DARLING_OVERLAY to your overlay dir}"

SDK_FLAT="${BUILD}/sdk-flat"
STAGED_OVERLAY="${BUILD}/staged-overlay"

rm -rf "${BUILD}"
mkdir -p "${SDK_FLAT}" "${STAGED_OVERLAY}/usr/lib/system"

# --- Unpack the pre-flattened SDK headers (see tests/vendor/README.md for
# --- why this can't just be -isysroot'd from the SDK tree directly: virtiofs
# --- can't resolve the SDK's symlinks, and re-flattening them from the guest
# --- risks the fuse_msgbuf OOM incident documented there). ---
tar xzf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${SDK_FLAT}"

# --- Stage libSystem.B.dylib + its usr/lib/system/ dependency closure as
# --- regular files (same virtiofs-symlink reasoning as launch-dynamic-smoke.c:
# --- readlink() over the mount is broken, so copy with find|pax, never cp -a). ---
cp "${OVERLAY}/usr/lib/libSystem.B.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
cp "${OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib"
ln -sf libobjc.A.dylib "${STAGED_OVERLAY}/usr/lib/libobjc.dylib"
cp "${OVERLAY}/usr/lib/libicucore.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib"
cp "${OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib"
cp "${OVERLAY}/usr/lib/libc++abi.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib"
(cd "${OVERLAY}/usr/lib/system" && find . -maxdepth 1 -type f | pax -rw "${STAGED_OVERLAY}/usr/lib/system")
# CoreFoundation.framework's actual binary lives at the bottom of a chain of
# framework-bundle convenience symlinks (Versions/Current -> A, etc.) that
# virtiofs can't resolve (see tests/vendor/README.md); the real regular file
# is always at Versions/A/<Name> directly.
mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A"
cp "${OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"

CLANG_FLAGS="-target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1"
CLANG_FLAGS="${CLANG_FLAGS} -I${SDK_FLAT}/usr/include -I${SRC}/tests/vendor/fakesdk"
LD_FLAGS="-arch x86_64 -platform_version macos 10.12 10.12 -syslibroot ${STAGED_OVERLAY} -Z"
COCOTRON="${SRC}/src/external/cocotron"

echo "=== hello-cctools-macho ==="
clang ${CLANG_FLAGS} -O1 -w -c "${SRC}/tests/hello-cctools.c" -o "${BUILD}/hello-cctools.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-cctools-macho" \
    "${BUILD}/hello-cctools.o" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-cctools-macho"
file "${SRC}/tests/hello-cctools-macho"

echo "=== sqlite3-real-macho ==="
# SQLite-specific config, matching its own CMakeLists.txt where applicable:
#   SQLITE_THREADSAFE=0        — single-threaded; Darling's pthreads route
#                                through Mach traps to darlingserver, a
#                                separate, not-yet-exercised path (#198).
#   SQLITE_OMIT_LOAD_EXTENSION — no dlopen() of extension .dylibs.
SQLITE_FLAGS="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION"
clang ${CLANG_FLAGS} ${SQLITE_FLAGS} -O1 -w \
    -c "${SRC}/src/external/sqlite/sqlite3.c" -o "${BUILD}/sqlite3.o"
clang ${CLANG_FLAGS} ${SQLITE_FLAGS} -O1 -w \
    -I"${SRC}/src/external/sqlite" \
    -c "${SRC}/src/external/sqlite/shell.c" -o "${BUILD}/shell.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/sqlite3-real-macho" \
    "${BUILD}/sqlite3.o" "${BUILD}/shell.o" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/sqlite3-real-macho"
file "${SRC}/tests/sqlite3-real-macho"

echo "=== hello-objc-macho ==="
clang ${CLANG_FLAGS} -fobjc-runtime=macosx-10.12 -x objective-c -O1 -w \
    -c "${SRC}/tests/hello-objc.m" -o "${BUILD}/hello-objc.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-objc-macho" \
    "${BUILD}/hello-objc.o" "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-objc-macho"
file "${SRC}/tests/hello-objc-macho"

echo "=== hello-cf-macho ==="
# Individual headers, not <CoreFoundation/CoreFoundation.h> — that umbrella
# is a symlink to a file that doesn't exist in the vendored submodule
# checkout (see tests/hello-cf.c's comment).
clang ${CLANG_FLAGS} -F"${SDK_FLAT}/Frameworks" -O1 -w \
    -c "${SRC}/tests/hello-cf.c" -o "${BUILD}/hello-cf.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-cf-macho" \
    "${BUILD}/hello-cf.o" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-cf-macho"
file "${SRC}/tests/hello-cf-macho"

echo "=== Foundation.dylib + hello-foundation-macho ==="
FOUND="${SRC}/src/external/foundation"
CF="${SRC}/src/external/corefoundation"
FOUND_FLAGS="${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fexceptions -fobjc-runtime=macosx-10.12"
FOUND_FLAGS="${FOUND_FLAGS} -include ${CF}/CoreFoundation_Prefix.h -include ${CF}/macros.h"
FOUND_FLAGS="${FOUND_FLAGS} -I${SDK_FLAT}/corefoundation-headers"
FOUND_FLAGS="${FOUND_FLAGS} -I${FOUND}/include -I${FOUND}/include/Foundation -I${FOUND}/src"
FOUND_FLAGS="${FOUND_FLAGS} -DNSBUILDINGFOUNDATION=1 -DINCLUDE_OBJC -DDEPLOYMENT_TARGET_MACOSX=1"
FOUND_FLAGS="${FOUND_FLAGS} -D__CONSTANT_CFSTRINGS__=1 -D__CONSTANT_STRINGS__=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=1"
FOUND_FLAGS="${FOUND_FLAGS} -DPAGE_SIZE=4096 -DDARLING -F${SDK_FLAT}/Frameworks"

mkdir -p "${BUILD}/found-build"
found_objs=""
# Two known, documented exclusions out of 236 source files listed in
# src/external/foundation/CMakeLists.txt's foundation_sources — both are
# genuine content gaps, not build-config mistakes:
#   NSTask.m        needs System/machine/cpu_capabilities.h, which no
#                    symlink anywhere in the vendored SDK resolves to (not
#                    a missing submodule init — the file plain isn't there).
#                    Also moot: NSTask needs fork/exec, unimplemented in
#                    mldr's syscall layer regardless.
#   NSNetServices.m  needs CFNetwork/CFNetServices.h, a real functional
#                    Bonjour/network-service-discovery API — unlike the
#                    type/constant-only CoreGraphics and CFNetwork-error-
#                    code stubs elsewhere in tests/vendor/fakesdk, this
#                    would need an actual implementation, not just headers.
python3 - "${FOUND}/CMakeLists.txt" <<'PYEOF' > "${BUILD}/foundation_sources.txt"
import re, sys
txt = open(sys.argv[1]).read()
m = re.search(r"set\(foundation_sources\n(.*?)\n\)", txt, re.S)
excluded = {"src/NSTask.m", "src/NSNetServices.m"}
seen = set()
for line in m.group(1).splitlines():
	f = line.split("#")[0].strip()
	# NSXPCConnection.m is genuinely listed twice in the vendored
	# CMakeLists.txt (harmless for CMake's own dedup, but ld64.lld errors
	# on the resulting duplicate-symbol object file if compiled/linked
	# twice) — dedupe defensively rather than assuming this is the only case.
	if f and f not in excluded and f not in seen:
		seen.add(f)
		print(f)
PYEOF

while IFS= read -r rel; do
	[ -z "${rel}" ] && continue
	extra=""
	case "${rel}" in
		*.c) extra="-x objective-c" ;;
	esac
	obj="${BUILD}/found-build/$(echo "${rel}" | tr '/' '_').o"
	clang ${FOUND_FLAGS} ${extra} -w -O0 -c "${FOUND}/${rel}" -o "${obj}"
	found_objs="${found_objs} ${obj}"
done < "${BUILD}/foundation_sources.txt"

mkdir -p "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
# shellcheck disable=SC2086
ld64.lld ${LD_FLAGS} -dylib \
	-install_name /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation \
	-o "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	${found_objs} \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"

mkdir -p "${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C"
cp "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"

clang ${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
	-F"${SDK_FLAT}/Frameworks" -I"${FOUND}/include" -x objective-c -O1 -w \
	-c "${SRC}/tests/hello-foundation.m" -o "${BUILD}/hello-foundation.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-foundation-macho" \
	"${BUILD}/hello-foundation.o" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
	"${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib" \
	"${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-foundation-macho"
file "${SRC}/tests/hello-foundation-macho"

echo "=== hello-appkit-macho ==="
# Stage AppKit and its dependency closure (including the stub frameworks
# built by build-gui.sh) into the staged overlay so ld64 can link against
# them and dyld can resolve them at runtime.
for fw in AppKit CoreGraphics Onyx2D CoreData QuartzCore ImageIO CoreText LaunchServices; do
    src_path="${OVERLAY}/System/Library"
    case "${fw}" in
        Onyx2D) src_path="${src_path}/PrivateFrameworks/${fw}.framework" ;;
        *)      src_path="${src_path}/Frameworks/${fw}.framework" ;;
    esac
    if [ -d "${src_path}" ]; then
        dst_path="${STAGED_OVERLAY}${src_path#${OVERLAY}}"
        mkdir -p "${dst_path}"
        find "${src_path}" -type f | pax -rw "${dst_path}"
    fi
done
# Native wrapper dylibs AppKit links against at runtime.
mkdir -p "${STAGED_OVERLAY}/usr/lib/native"
for wrap in libGL.dylib libFreeType.dylib libfontconfig.dylib; do
    if [ -f "${OVERLAY}/usr/lib/native/${wrap}" ]; then
        cp "${OVERLAY}/usr/lib/native/${wrap}" "${STAGED_OVERLAY}/usr/lib/native/${wrap}"
    fi
done
# libdispatch/libunwind are in usr/lib/system and already staged above.

clang ${CLANG_FLAGS} -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING \
    -F"${SDK_FLAT}/Frameworks" -I"${FOUND}/include" -I"${COCOTRON}/AppKit/include" -I"${SRC}/src/freebsd-shims/missing-headers" \
    -x objective-c -O1 -w \
    -c "${SRC}/tests/hello-appkit.m" -o "${BUILD}/hello-appkit.o"
ld64.lld ${LD_FLAGS} -o "${SRC}/tests/hello-appkit-macho" \
    "${BUILD}/hello-appkit.o" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
    "${STAGED_OVERLAY}/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
    "${STAGED_OVERLAY}/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
    "${STAGED_OVERLAY}/usr/lib/libobjc.A.dylib" "${STAGED_OVERLAY}/usr/lib/libicucore.A.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libc++.1.dylib" "${STAGED_OVERLAY}/usr/lib/libc++abi.dylib" \
    "${STAGED_OVERLAY}/usr/lib/libSystem.B.dylib"
chmod 755 "${SRC}/tests/hello-appkit-macho"
file "${SRC}/tests/hello-appkit-macho"

echo "=== done ==="
echo "Run with: echo 'select 21*2;' | DARLING_TEST_BINARY=sqlite3-real-macho <launch-dynamic-smoke binary>"
echo "Run with: DARLING_TEST_BINARY=hello-objc-macho <launch-dynamic-smoke binary>"
echo "Run with: DARLING_TEST_BINARY=hello-cf-macho <launch-dynamic-smoke binary>"
echo "Run with: DARLING_TEST_BINARY=hello-foundation-macho <launch-dynamic-smoke binary>"
echo "Run with: DARLING_TEST_BINARY=hello-appkit-macho <launch-dynamic-smoke binary>"
