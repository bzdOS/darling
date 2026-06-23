#!/bin/sh
# Build only mldr-real (darlingserver already built at /var/darling-build/dserver/).
# Run on the build VM: sh /path/to/darling/build-freebsd/build-mldr-only.sh
set -e

BUILD=/var/darling-build/dserver
MLDR_REAL_BUILD="${BUILD}/mldr-real"
SRC=/path/to/darling/src
DS=${SRC}/external/darlingserver
DRPC=${DS}/generated-rpc

rm -rf "${MLDR_REAL_BUILD}" && mkdir -p "${MLDR_REAL_BUILD}" && cd "${MLDR_REAL_BUILD}"

cat > CMakeLists.txt << 'MLDR_EOF'
cmake_minimum_required(VERSION 3.13)
project(mldr_real C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -ggdb -O2")

set(SRC  /path/to/darling/src)
set(DS   /path/to/darling/src/external/darlingserver)
set(DRPC ${DS}/generated-rpc)

# Generate darling-config.h (needed by stack.c)
set(CMAKE_INSTALL_PREFIX "/usr/local/darling-overlay")
set(CMAKE_INSTALL_LIBDIR "lib")
set(SUFFIX "")
set(GIT_BRANCH "pr-arm64")
set(GIT_COMMIT_HASH "freebsd-port")
configure_file(
  ${SRC}/include/darling-config.h.in
  ${CMAKE_BINARY_DIR}/darling-config.h
)

# cctools/include MUST come first: mldr/include/mach-o/loader.h is a broken
# 9-level-up symlink that exits the 9p mount boundary → EMSGSIZE on FreeBSD.
include_directories(
  ${SRC}/external/cctools-port/cctools/include
  ${SRC}/startup/mldr/include
  ${SRC}/startup/mldr
  ${DRPC}/include
  ${DS}/include
  ${CMAKE_BINARY_DIR}
)

add_definitions(
  -DDARLING_FREEBSD
  -D_GNU_SOURCE
  -DINSTALL_PREFIX="/usr/local/darling-overlay"
  -DSYSTEM_ROOT="/Volumes/SystemRoot"
  -DLIBEXEC_PATH="/usr/local/darling-overlay/libexec/darling"
  # FreeBSD uses bswap32(), Linux uses __bswap_32() — map for mldr.c SWAP32 macro
  -D__bswap_32=bswap32
)

add_executable(mldr
  ${SRC}/startup/mldr/mldr.c
  ${SRC}/startup/mldr/commpage.c
  ${DRPC}/src/rpc.c
  ${SRC}/startup/mldr/elfcalls/elfcalls.c
  ${SRC}/startup/mldr/elfcalls/threads.c
)

# rpc.c needs dserver_rpc_hooks_* macros defined in dserver-rpc-defs.h.
# Force-include it so it arrives before rpc.c's own #include <darlingserver/rpc-supplement.h>.
target_compile_options(mldr PRIVATE
  -include ${SRC}/startup/mldr/resources/dserver-rpc-defs.h
)

target_link_libraries(mldr PRIVATE -lc -lpthread)
MLDR_EOF

cmake .
make -j$(sysctl -n hw.ncpu)
echo "=== Built: ${MLDR_REAL_BUILD}/mldr ==="
ls -lh "${MLDR_REAL_BUILD}/mldr"
