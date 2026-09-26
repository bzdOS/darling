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

# -nostdinc: remove all default search paths.
# -include: force-include compat header before anything else (blocks Darwin types)
# -D_BSD_I386__TYPES_H_: skip Darwin's i386/_types.h entirely
# Then add include paths in order: FreeBSD first, then darling
set(CMAKE_C_FLAGS_INIT
  "-nostdinc \
   -include ${DARLING_TOOLS}/freebsd_mig_compat.h \
   -D_BSD_I386__TYPES_H_ \
   -isystem /usr/include \
   -isystem /usr/lib/clang/19/include \
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
   -isystem /usr/lib/clang/19/include \
   -isystem /usr/local/llvm19/lib/clang/19/include \
   -isystem ${DARLING_ROOT}/basic-headers \
   -isystem ${DARLING_ROOT}/src/external/bootstrap_cmds/darling/include/mach \
   -isystem ${DARLING_ROOT}/src/external/bootstrap_cmds/darling/include \
   -isystem ${DARLING_ROOT}/src/startup/mldr/include \
   -isystem ${DARLING_ROOT}/src/external/cctools-port/cctools/include/foreign")

set(CMAKE_FIND_ROOT_PATH /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
