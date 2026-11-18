#!/bin/sh
# build-appkit-sync-test.sh - static check (compiles nothing, runs nothing from either script): the AppKit step of
# build-appkit.sh must stay a faithful copy of build-gui.sh's. Compared: the compiler flags (CLANG_BASE, SDK_*),
# the AppKit include flags (AK, AK_FLAGS), the sources variable and the compile_component call, the bodies of
# extract_sources and compile_component, LD_FLAGS and the AppKit link line (with ${STAGED_OVERLAY} read as ${OVERLAY},
# the one intended difference, and without the -o line), and that the AppKit_sources list exists in cocotron.
# Prints BUILD-APPKIT-SYNC-TEST pass|fail.
# usage: sh build-freebsd/build-appkit-sync-test.sh   (env DARLING_SRC_DIR overrides the repository root)
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${DARLING_SRC_DIR:-$(cd "${HERE}/.." && pwd)}"
G="${HERE}/build-gui.sh"; A="${HERE}/build-appkit.sh"
fails=""
norm() { sed -e 's/\${STAGED_OVERLAY}/${OVERLAY}/g'; }
cmp_part() { # name, command producing the part from file $1
	_n="$1"; shift
	_g=$("$@" "${G}" | norm); _a=$("$@" "${A}" | norm)
	if [ -z "${_g}" ] || [ -z "${_a}" ]; then fails="${fails} ${_n}(empty)"; return; fi
	[ "${_g}" = "${_a}" ] || fails="${fails} ${_n}"
}
p_flags() { grep -E '^(CLANG_BASE|SDK_SYSTEM_INCLUDES|SDK_INCLUDES|COCOTRON|SDK_FLAT)=' "$1"; }
p_ak() { grep -E '^(AK|AK_FLAGS)=' "$1"; }
p_call() { grep -E '^ak_objs=\$\(compile_component AppKit ' "$1"; }
p_ldflags() { grep -E '^LD_FLAGS=' "$1"; }
p_extract() { sed -n '/^extract_sources() {/,/^}/p' "$1"; }
p_compile() { sed -n '/^compile_component() {/,/^}/p' "$1"; }
p_link() { sed -n '/-install_name \/System\/Library\/Frameworks\/AppKit.framework/,/libfontconfig.dylib"$/p' "$1" | grep -v -e '^[[:space:]]*-o '; }
cmp_part flags p_flags
cmp_part ak-flags p_ak
cmp_part sources-call p_call
cmp_part ld-flags-modulo-sysroot p_ldflags
cmp_part extract_sources p_extract
cmp_part compile_component p_compile
cmp_part link-line p_link
# AppKit_sources must still be a set(...) list in cocotron's CMakeLists.txt, and both scripts must name it.
grep -q '^set(AppKit_sources$' "${SRC}/src/external/cocotron/AppKit/CMakeLists.txt" || fails="${fails} AppKit_sources-def"
grep -q 'AppKit_sources' "${G}" && grep -q 'AppKit_sources' "${A}" || fails="${fails} AppKit_sources-name"
# the sysroot is the only allowed LD_FLAGS difference
[ "$(grep -E '^LD_FLAGS=' "${G}")" != "$(grep -E '^LD_FLAGS=' "${A}")" ] || fails="${fails} ld-flags-should-differ-in-sysroot"
if [ -z "${fails}" ]; then echo "BUILD-APPKIT-SYNC-TEST pass"; exit 0; fi
echo "BUILD-APPKIT-SYNC-TEST fail:${fails}"; exit 1
