project(vchroot)

# vchroot is a tiny Mach-O helper: darlingserver execs it (via mldr) to build
# the vchroot context and exec the guest init. It must build WITHOUT the global
# freebsd-nostdinc host-tool toolchain the rest of this tree inherits: that
# toolchain force-includes freebsd_mig_compat.h and defines _BSD_I386__TYPES_H_,
# which suppresses i386/_types.h and its __int64_t/__int32_t — so vchroot.c (and
# libplatform) fail under it. Left alone, vchroot.c compiles clean.
#
# Mechanism (chosen): compile against the macOS SDK and link the prebuilt
# overlay libSystem, exactly as build-crash-probe.sh builds a guest Mach-O, via
# explicit clang + ld64.lld commands. This deliberately avoids
# add_darling_executable / the in-tree `system` target (which drag in the whole
# libSystem world the broken toolchain cannot build) and avoids CMake's default
# link rule (which injects --dependency-file that ld64.lld rejects).
# `ninja vchroot` therefore builds only this file.

# The overlay holding the prebuilt usr/lib/libSystem.B.dylib, via the
# environment (same convention as build-crash-probe.sh) or -DDARLING_OVERLAY=.
if(NOT DEFINED DARLING_OVERLAY)
	set(DARLING_OVERLAY "$ENV{DARLING_OVERLAY}")
endif()
if(NOT DARLING_OVERLAY)
	message(FATAL_ERROR "set DARLING_OVERLAY (env or -DDARLING_OVERLAY) to the overlay holding usr/lib/libSystem.B.dylib")
endif()

# SDK headers live in the source tree; point at them directly rather than the
# FreeBSD /usr/include the host-tool toolchain would force.
set(MACOSX_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")

find_program(VCHROOT_LD64_LLD ld64.lld)
if(NOT VCHROOT_LD64_LLD)
	set(VCHROOT_LD64_LLD "/usr/local/bin/ld64.lld")
endif()

# Output into bin/ so the file name does not collide with the phony target
# name `vchroot` (ninja rejects a phony target that shares its name with an
# input). install(FILES) keeps the basename, so it still lands as `vchroot`.
set(VCHROOT_BIN "${CMAKE_CURRENT_BINARY_DIR}/bin/vchroot")

# Plain Darwin libc symbols (no $UNIX2003 suffixes): __DARWIN_UNIX03=0 matches
# what the prebuilt overlay libSystem exports. LC_MAIN, no crt1 — ld64
# synthesises the entry, mirroring the overlay's own vchroot.
add_custom_command(
	OUTPUT "${VCHROOT_BIN}"
	COMMAND ${CMAKE_C_COMPILER}
		-target x86_64-apple-darwin20 -nostdinc -mmacosx-version-min=11.0
		-D__DARWIN_UNIX03=0 -w
		-isystem "${MACOSX_SDK}/usr/include"
		-c "${CMAKE_CURRENT_SOURCE_DIR}/vchroot.c"
		-o "${CMAKE_CURRENT_BINARY_DIR}/vchroot.o"
	COMMAND ${VCHROOT_LD64_LLD}
		-arch x86_64 -platform_version macos 11.0 11.0
		-syslibroot "${DARLING_OVERLAY}" -Z
		-o "${VCHROOT_BIN}"
		"${CMAKE_CURRENT_BINARY_DIR}/vchroot.o"
		"${DARLING_OVERLAY}/usr/lib/libSystem.B.dylib"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vchroot.c"
	COMMENT "Building vchroot (standalone Mach-O, overlay prebuilt libSystem)"
	VERBATIM)

add_custom_target(vchroot DEPENDS "${VCHROOT_BIN}")

install(FILES "${VCHROOT_BIN}" DESTINATION libexec/darling/usr/libexec/darling
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
	            GROUP_READ GROUP_EXECUTE
	            WORLD_READ WORLD_EXECUTE)
