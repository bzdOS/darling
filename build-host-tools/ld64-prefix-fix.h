/* Force-included shim for the standalone host ld64: ld64/src/CMakeLists.txt:28
   emits -DPROGRAM_PREFIX="${APPLE_TARGET_TRIPLET_PRIMARY}-" with quotes the
   shell strips, so Options.cpp:6325 fails to compile. Re-define it as a string. */
#undef PROGRAM_PREFIX
#define PROGRAM_PREFIX "x86_64-apple-darwin20-"
