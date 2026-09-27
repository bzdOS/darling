# Toolchain: build host tools on FreeBSD with darling mach headers,
# but without type conflicts between Darwin and FreeBSD.

set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER /usr/bin/cc)
set(CMAKE_CXX_COMPILER /usr/bin/c++)

# Derive the checkout location instead of hardcoding a machine-specific prefix,
# so this toolchain file works wherever the tree is checked out.
get_filename_component(DARLING_TOOLS "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(DARLING_ROOT "${DARLING_TOOLS}/.." ABSOLUTE)

# Resource dirs are asked of the compilers rather than written out. The include
# path moves with every clang upgrade, and naming a version in a file also says
# which toolchain the file was written against. Each compiler can be pointed
# elsewhere if a host lays them out differently.
if(NOT DEFINED DARLING_CLANG)
    set(DARLING_CLANG /usr/bin/cc)
endif()
if(NOT DEFINED DARLING_CLANGXX)
    set(DARLING_CLANGXX /usr/bin/c++)
endif()
foreach(_pair "DARLING_CLANG_RESOURCE_DIR;${DARLING_CLANG}"
              "DARLING_CLANGXX_RESOURCE_DIR;${DARLING_CLANGXX}")
    list(GET _pair 0 _var)
    list(GET _pair 1 _cc)
    execute_process(
        COMMAND "${_cc}" -print-resource-dir
        OUTPUT_VARIABLE ${_var}
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endforeach()
# The C++ flags may also want a resource dir from a second clang, if the host
# keeps one next to the system compiler. Name it when configuring
# (-DDARLING_EXTRA_CLANG=<path to that clang>); with nothing named, the flag is
# left out rather than pointing at a path that does not exist.
set(DARLING_EXTRA_CLANG_INCLUDE "")
if(DEFINED DARLING_EXTRA_CLANG)
    execute_process(
        COMMAND "${DARLING_EXTRA_CLANG}" -print-resource-dir
        OUTPUT_VARIABLE _extra_resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_extra_resource_dir)
        set(DARLING_EXTRA_CLANG_INCLUDE "-isystem ${_extra_resource_dir}/include")
    endif()
endif()

# -nostdinc: remove all default search paths.
# -include: force-include compat header before anything else (blocks Darwin types)
# -D_BSD_I386__TYPES_H_: skip Darwin's i386/_types.h entirely
# Then add include paths in order: FreeBSD first, then darling
set(CMAKE_C_FLAGS_INIT
  "-nostdinc \
   -include ${DARLING_TOOLS}/freebsd_mig_compat.h \
   -D_BSD_I386__TYPES_H_ \
   -isystem /usr/include \
   -isystem ${DARLING_CLANG_RESOURCE_DIR}/include \
   -isystem ${DARLING_ROOT}/basic-headers \
   -isystem ${DARLING_ROOT}/src/external/bootstrap_cmds/darling/include/mach \
   -isystem ${DARLING_ROOT}/src/external/bootstrap_cmds/darling/include \
   -isystem ${DARLING_ROOT}/src/startup/mldr/include \
   -isystem ${DARLING_ROOT}/src/external/cctools-port/cctools/include/foreign")
set(CMAKE_CXX_FLAGS_INIT
  "-nostdinc++ -nostdinc \
   -include ${DARLING_TOOLS}/freebsd_mig_compat.h \
   -D_BSD_I386__TYPES_H_ \
   -isystem /usr/include \
   -isystem ${DARLING_CLANGXX_RESOURCE_DIR}/include \
   ${DARLING_EXTRA_CLANG_INCLUDE} \
   -isystem ${DARLING_ROOT}/basic-headers \
   -isystem ${DARLING_ROOT}/src/external/bootstrap_cmds/darling/include/mach \
   -isystem ${DARLING_ROOT}/src/external/bootstrap_cmds/darling/include \
   -isystem ${DARLING_ROOT}/src/startup/mldr/include \
   -isystem ${DARLING_ROOT}/src/external/cctools-port/cctools/include/foreign")

set(CMAKE_FIND_ROOT_PATH /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
