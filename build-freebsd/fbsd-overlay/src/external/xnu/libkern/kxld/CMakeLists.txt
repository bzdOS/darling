# kxld — kernel extension linker (macOS only, stub on FreeBSD)
#
# purpose: kxld is only used for macOS kernel extension loading.
#          FreeBSD uses kldload(2) natively; this target is a no-op.
# input:   FREEBSD_COMPAT
# output:  empty static library kxld (satisfies link deps)
# sideEffects: none

cmake_minimum_required(VERSION 3.13)

if(FREEBSD_COMPAT)
    message(STATUS "[FreeBSD.cmake] kxld: stub (kext loading not needed on FreeBSD)")
    # Provide an empty target so any target_link_libraries(... kxld) doesn't break.
    add_library(kxld STATIC ${CMAKE_CURRENT_BINARY_DIR}/kxld_stub.c)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kxld_stub.c"
        "/* kxld stub for FreeBSD — kext loading handled by kld */\n")
else()
    message(FATAL_ERROR "kxld FreeBSD stub included on non-FreeBSD host")
endif()
