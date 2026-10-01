# FreeBSD platform detection and configuration for Darling
# Included by CMakeLists.txt when CMAKE_SYSTEM_NAME is FreeBSD or host uname -s == FreeBSD
#
# purpose: Detect FreeBSD host, set pkg-config paths, disable Linux-only build
#          components, and inject FreeBSD compat flags.
# input:   CMAKE_C_COMPILER, CMAKE_SYSTEM_NAME, _HOST_ARCH
# output:  FREEBSD_COMPAT (BOOL), DARLING_FREEBSD_COMPAT_DIR, adjusted
#          CMAKE_C_FLAGS / CMAKE_CXX_FLAGS, disabled subdirs for
#          Linux-specific components.
# sideEffects: Appends to CMAKE_REQUIRED_INCLUDES; sets pkg-config prefix.

# ── Host detection ────────────────────────────────────────────────────────────
if(NOT DEFINED FREEBSD_COMPAT)
    execute_process(
        COMMAND uname -s
        OUTPUT_VARIABLE _UNAME_S
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_UNAME_S STREQUAL "FreeBSD")
        set(FREEBSD_COMPAT TRUE CACHE BOOL "Building on FreeBSD host")
    else()
        set(FREEBSD_COMPAT FALSE CACHE BOOL "Building on FreeBSD host")
    endif()
endif()

if(NOT FREEBSD_COMPAT)
    return()
endif()

message(STATUS "[FreeBSD.cmake] FreeBSD host detected — activating compat layer")

# ── FreeBSD version ───────────────────────────────────────────────────────────
execute_process(
    COMMAND uname -r
    OUTPUT_VARIABLE FREEBSD_RELEASE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "[FreeBSD.cmake] FreeBSD release: ${FREEBSD_RELEASE}")

# Extract major version (e.g. "15.1-RELEASE" -> 15)
string(REGEX MATCH "^([0-9]+)" FREEBSD_VERSION_MAJOR "${FREEBSD_RELEASE}")
if(FREEBSD_VERSION_MAJOR LESS 14)
    message(FATAL_ERROR "[FreeBSD.cmake] FreeBSD 14+ required (found ${FREEBSD_RELEASE})")
endif()

# ── pkg-config paths ──────────────────────────────────────────────────────────
# FreeBSD installs ports under /usr/local; override the default Linux search
# paths so find_package() and pkg_check_modules() find things correctly.
set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
set(CMAKE_PREFIX_PATH "/usr/local" ${CMAKE_PREFIX_PATH})
set(CMAKE_INCLUDE_PATH "/usr/local/include" ${CMAKE_INCLUDE_PATH})
set(CMAKE_LIBRARY_PATH "/usr/local/lib" ${CMAKE_LIBRARY_PATH})

include(FindPkgConfig OPTIONAL)

# ── Compat include directory ──────────────────────────────────────────────────
# src/startup/freebsd_compat.h lives here; add to compiler search path.
# src/startup/compat-includes/ contains shims for Linux-only headers that don't
# exist on FreeBSD (alloca.h, adjusted sched.h, etc.).  Must come BEFORE system
# headers so the shims are found first.
set(DARLING_FREEBSD_COMPAT_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/startup")
set(DARLING_FREEBSD_SHIM_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/../src/startup/compat-includes")
list(APPEND CMAKE_REQUIRED_INCLUDES "${DARLING_FREEBSD_COMPAT_DIR}" "${DARLING_FREEBSD_SHIM_INCLUDES}")

# ── Compiler flags ────────────────────────────────────────────────────────────
# -DDARLING_FREEBSD — main compile-time guard used by compat headers/sources
# -D__BSD_VISIBLE   — expose BSD-specific POSIX extensions in <sys/*.h>
# -D_BSD_SOURCE     — some ports headers still use this guard
# -I/usr/local/include — explicit; cmake prefix path may lag behind pkg-config
# compat-includes MUST precede system headers to shadow Linux-only headers
set(FREEBSD_COMPAT_FLAGS
    "-DDARLING_FREEBSD"
    "-D__BSD_VISIBLE=1"
    "-D_BSD_SOURCE"
    "-I${DARLING_FREEBSD_SHIM_INCLUDES}"
    "-I${DARLING_FREEBSD_COMPAT_DIR}"
    "-I/usr/local/include"
    "-include ${DARLING_FREEBSD_COMPAT_DIR}/freebsd_compat.h"
)
string(JOIN " " FREEBSD_COMPAT_FLAGS_STR ${FREEBSD_COMPAT_FLAGS})

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   ${FREEBSD_COMPAT_FLAGS_STR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FREEBSD_COMPAT_FLAGS_STR}")

# ── Disable Linux-only components ─────────────────────────────────────────────
# These components depend on Linux kernel interfaces that do not exist on
# FreeBSD.  Each is guarded by a CMake option so it can still be turned on
# manually if a port is created later.

option(DARLING_BUILD_KERNEL_MODULE
    "Build the darlingserver kernel module (Linux only)" OFF)
option(DARLING_BUILD_FSEVENTS_LINUX
    "Build FSEvents Linux fanotify backend (Linux only)" OFF)
option(DARLING_BUILD_OVERLAY
    "Build overlayfs-backed prefix mounts (Linux only)" OFF)

if(DARLING_BUILD_KERNEL_MODULE)
    message(WARNING "[FreeBSD.cmake] DARLING_BUILD_KERNEL_MODULE is ON but we are on FreeBSD — forcing OFF")
    set(DARLING_BUILD_KERNEL_MODULE OFF CACHE BOOL "" FORCE)
endif()

# Macro used by subdirectory CMakeLists.txt files that contain Linux-only code.
# Usage: darling_require_linux() at the top of the file will skip it silently
# when FREEBSD_COMPAT is TRUE.
macro(darling_require_linux)
    if(FREEBSD_COMPAT)
        message(STATUS "[FreeBSD.cmake] Skipping Linux-only subdirectory: ${CMAKE_CURRENT_SOURCE_DIR}")
        return()
    endif()
endmacro()

# ── /proc emulation note ──────────────────────────────────────────────────────
# FreeBSD mounts procfs at /proc by default only when enabled in fstab.
# Darling startup code reads /proc/<pid>/comm and /proc/<pid>/status.
# The freebsd_compat.h shim replaces those paths with sysctl-based equivalents.
# At runtime, ensure procfs is mounted:
#   mount -t procfs proc /proc
# or add to /etc/fstab:
#   proc  /proc  procfs  rw  0  0
message(STATUS "[FreeBSD.cmake] NOTE: Darling on FreeBSD requires procfs mounted at /proc")
message(STATUS "[FreeBSD.cmake]       Add to /etc/fstab: proc  /proc  procfs  rw  0  0")

# ── setcap replacement ────────────────────────────────────────────────────────
# FreeBSD uses setuid rather than Linux capabilities.  The FindSetcap module
# won't find anything on FreeBSD; override the result so the build doesn't fail.
set(SETCAP_FOUND FALSE)
set(SETCAP_EXECUTABLE "" CACHE STRING "setcap not available on FreeBSD — use setuid instead" FORCE)

# ── PulseAudio / OSS ──────────────────────────────────────────────────────────
# FreeBSD uses OSS (not PulseAudio) as the primary audio API.  If the user
# hasn't installed audio/pulseaudio from ports, FindPulseAudio will fail.
# We set PULSEAUDIO_FOUND=FALSE to trigger the NO-audio fallback path instead
# of a hard configure error.  Users who have ports' pulseaudio installed can
# override with -DDARLING_FORCE_PULSEAUDIO=ON.
option(DARLING_FORCE_PULSEAUDIO "Force PulseAudio even on FreeBSD" OFF)
if(NOT DARLING_FORCE_PULSEAUDIO)
    set(PULSEAUDIO_FOUND FALSE CACHE BOOL "" FORCE)
    message(STATUS "[FreeBSD.cmake] PulseAudio disabled; CoreAudio will use OSS backend")
endif()

# ── pty.h ─────────────────────────────────────────────────────────────────────
# Linux <pty.h> provides openpty()/forkpty().  On FreeBSD these live in
# <libutil.h> and require linking -lutil.  The compat header handles the
# include redirect; we add -lutil to the link flags here.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lutil")

# ── alloca.h ─────────────────────────────────────────────────────────────────
# On FreeBSD alloca is declared in <stdlib.h>; <alloca.h> doesn't exist.
# The compat header provides the redirect — no linker change needed.

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "[FreeBSD.cmake] Configuration complete")
message(STATUS "[FreeBSD.cmake]   Compat dir  : ${DARLING_FREEBSD_COMPAT_DIR}")
message(STATUS "[FreeBSD.cmake]   C flags     : ${FREEBSD_COMPAT_FLAGS_STR}")
message(STATUS "[FreeBSD.cmake]   Kernel module: ${DARLING_BUILD_KERNEL_MODULE}")
message(STATUS "[FreeBSD.cmake]   FSEvents linux: ${DARLING_BUILD_FSEVENTS_LINUX}")
