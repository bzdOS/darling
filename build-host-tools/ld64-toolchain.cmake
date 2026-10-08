# Minimal toolchain for standalone ld64 build on FreeBSD.
# The ld64 CMakeLists.txt has its own FREEBSD_COMPAT handling for includes.
# This toolchain only provides the cross-compile variables.

set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER /usr/bin/cc)
set(CMAKE_CXX_COMPILER /usr/bin/c++)

# The ld64 project needs TARGET_x86_64 to be set, and the triplet.
# -DDARLING selects the DARLING form of Atom::fixupsEnd()/fixupsBegin()
# (machofile()._fixups.data()+..., macho_relocatable_file.cpp:759): the plain
# operator[] form trips the libc++ hardening assert on the legitimate
# one-past-the-end index and dies with ud2/SIGILL.
set(CMAKE_C_FLAGS_INIT "-DDARLING")
set(CMAKE_CXX_FLAGS_INIT "-DDARLING")

set(CMAKE_FIND_ROOT_PATH /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
