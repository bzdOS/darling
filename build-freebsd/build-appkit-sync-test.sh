#!/bin/sh
# build-appkit-sync-test.sh - static check (compiles nothing, runs nothing from either script): the AppKit step of
# build-appkit.sh must stay a faithful copy of build-gui.sh's. Compared: the compiler flags (CLANG_BASE, SDK_*),
# the AppKit include flags (AK, AK_FLAGS), the sources variable and the compile_component call, the bodies of
# extract_sources and compile_component, LD_FLAGS and the AppKit link line (with ${STAGED_OVERLAY} read as ${OVERLAY},
# the one intended difference, and without the -o line), and that the AppKit_sources list exists in cocotron.
# Also compared: the full set of include-path flags (-I, -iquote, -isystem, -F, -include) on the AppKit compile
# path (every such token in the AK_FLAGS lines, CLANG_BASE and SDK_* lines, one per line, order-sensitive), and any
# header copy commands (cp/ln/install/rsync lines naming include or .h) in each script.
# Negative control: with BUILD_APPKIT_SYNC_NEGCTL=1 the test copies both scripts to a mktemp dir, removes the
# QuartzCore -I from the copy of build-appkit.sh, runs itself on the copies, and passes only if that run fails.
# Prints BUILD-APPKIT-SYNC-TEST pass|fail.
# usage: sh build-freebsd/build-appkit-sync-test.sh   (env DARLING_SRC_DIR overrides the repository root)
if [ "${BUILD_APPKIT_SYNC_NEGCTL:-}" = 1 ] && [ -z "${SYNC_TEST_SCRATCH:-}" ]; then
	_h="$(cd "$(dirname "$0")" && pwd)"; _t=$(mktemp -d) || exit 2
	cp "${_h}/build-gui.sh" "${_h}/build-appkit.sh" "${_h}/build-appkit-sync-test.sh" "${_t}/"
	sed -i.bak 's| -I${COCOTRON}/QuartzCore/include||' "${_t}/build-appkit.sh"
	if cmp -s "${_t}/build-appkit.sh" "${_t}/build-appkit.sh.bak"; then rm -rf "${_t}"; echo "BUILD-APPKIT-SYNC-TEST negctl fail: removal changed nothing"; exit 1; fi
	_o=$(SYNC_TEST_SCRATCH=1 DARLING_SRC_DIR="${DARLING_SRC_DIR:-$(cd "${_h}/.." && pwd)}" sh "${_t}/build-appkit-sync-test.sh"); _r=$?
	rm -rf "${_t}"
	if [ "${_r}" -ne 0 ]; then echo "BUILD-APPKIT-SYNC-TEST negctl pass (scratch copy failed as expected: ${_o})"; exit 0; fi
	echo "BUILD-APPKIT-SYNC-TEST negctl fail: scratch copy still passed"; exit 1
fi
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
p_incs() { grep -E '^(AK_FLAGS|CLANG_BASE|SDK_SYSTEM_INCLUDES|SDK_INCLUDES)=' "$1" | tr ' ' '\n' | grep -E '^(-I|-iquote|-isystem|-F|-include)'; }
p_hdrcp() { grep -E '^[[:space:]]*(cp|ln|install|rsync)[[:space:]].*(include|\.h)' "$1"; }
cmp_part flags p_flags
cmp_part include-paths p_incs
_g=$(p_hdrcp "${G}" | norm); _a=$(p_hdrcp "${A}" | norm)
[ "${_g}" = "${_a}" ] || fails="${fails} header-copies"
grep -q 'QuartzCore/include' "${G}" && grep -q 'QuartzCore/include' "${A}" || fails="${fails} quartzcore-include-missing"
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
