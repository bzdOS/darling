# libsystem_kernel FreeBSD shim
#
# purpose: Provide macOS libsystem_kernel ABI on FreeBSD using native BSD syscalls.
# input:   FREEBSD_COMPAT build flag
# output:  system_kernel target (shared library)
# sideEffects: none

cmake_minimum_required(VERSION 3.13)

if(FREEBSD_COMPAT)
    message(STATUS "[FreeBSD.cmake] libsystem_kernel: using FreeBSD native syscall shim")

    set(_SHIM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/freebsd_syscall_shim.c")
    if(NOT EXISTS "${_SHIM_SRC}")
        file(WRITE "${_SHIM_SRC}"
            "/* FreeBSD syscall shim for Darling libsystem_kernel */\nvoid _darling_libsystem_kernel_freebsd_init(void) {}\n")
    endif()

    add_library(system_kernel SHARED ${_SHIM_SRC})
    set_target_properties(system_kernel PROPERTIES OUTPUT_NAME "system_kernel" VERSION 1 SOVERSION 1)
    target_compile_options(system_kernel PRIVATE -DDARLING_FREEBSD -D__BSD_VISIBLE=1)
    install(TARGETS system_kernel LIBRARY DESTINATION libexec/darling/usr/lib/system/)
else()
    message(FATAL_ERROR "libsystem_kernel FreeBSD shim included on non-FreeBSD host")
endif()
