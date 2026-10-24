cmake_minimum_required(VERSION 3.18)
project(MetalStub C)

# FreeBSD 15.1 with Mesa GLES2 + EGL via pkg install mesa-libs mesa-dri
# Build: mkdir build && cd build && cmake .. && make

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

# FreeBSD: /usr/local is not in the default linker search path
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    link_directories(/usr/local/lib)
    include_directories(/usr/local/include)
endif()

# Headers
include_directories(${CMAKE_SOURCE_DIR}/include)

# EGL / GLES2 — provided by mesa-libs on FreeBSD
find_package(PkgConfig REQUIRED)
pkg_check_modules(EGL  REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)

# Library sources
set(METAL_SRCS
    src/MTLDevice.c
    src/MTLLibrary.c
    src/MTLBuffer.c
    src/MTLTexture.c
    src/MTLCommandQueue.c
    src/MTLRenderPipeline.c
)

add_library(metal_stub STATIC ${METAL_SRCS})
target_include_directories(metal_stub PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
)
target_link_libraries(metal_stub
    PkgConfig::EGL
    PkgConfig::GLES
)

# Triangle smoke test
add_executable(triangle test/triangle.c)
target_include_directories(triangle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
)
target_link_libraries(triangle metal_stub PkgConfig::EGL PkgConfig::GLES)

# CTest integration
enable_testing()
add_test(NAME triangle_smoke
         COMMAND triangle
         WORKING_DIRECTORY /tmp)
set_tests_properties(triangle_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "OK: /tmp/triangle.ppm written")
