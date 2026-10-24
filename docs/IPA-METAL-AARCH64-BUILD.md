# IPA M3 Metal stub — AArch64 cross-compile of libmetal_stub.a

## Goal

Verify the build of `ipa-runtime/Metal/` stubs (`MTLDevice`, `MTLCommandQueue`,
`MTLLibrary`) on the FreeBSD dev VM, producing **AArch64** (ARM64) object code
— NOT host x86-64.

## Trap

The pre-existing `libmetal_stub.a` in `build-freebsd/` is `ELF64 / Machine:
Advanced Micro Devices X86-64` — a host x86-64 build.  Per acceptance this does
**not** count: the ipa-runtime targets the ARM64 Darwin layer, so the stubs must
be `Machine: AArch64`.

## Build command (cross, raw clang)

```
clang --target=aarch64-unknown-freebsd15.0 \
      --sysroot=$SYSROOT_AARCH64_FREEBSD \
      -I<ipa-runtime>/Metal/include \
      -I/usr/local/include \
      -c <ipa-runtime>/Metal/src/<stub>.c \
      -o <build-dir>/<stub>.o
```

* `$SYSROOT_AARCH64_FREEBSD` — FreeBSD aarch64 sysroot providing the
  architecture-correct C headers (resolves `stdlib.h`, `stdint.h`, etc.).
* `-I<ipa-runtime>/Metal/include` — local `Metal/*.h` umbrella headers.
* `-I/usr/local/include` — EGL/GLES2 headers from mesa-libs (architecture-
  independent declarations; only headers are needed, no linking, since the
  output is a relocatable archive).
* Source files: `MTLDevice.c`, `MTLCommandQueue.c`, `MTLLibrary.c`,
  `MTLBuffer.c`, `MTLTexture.c`, `MTLRenderPipeline.c`.

Archive:

```
llvm-ar rcs libmetal_stub.a MTLDevice.o MTLCommandQueue.o MTLLibrary.o \
        MTLBuffer.o MTLTexture.o MTLRenderPipeline.o
```

## Result

All six objects and the archive verify as **AArch64**:

```
$ file libmetal_stub.a
libmetal_stub.a: current ar archive

$ readelf -h libmetal_stub.a | grep -iE 'Machine|Class'
  Class:                                                       ELF64
  Machine:                                                     AArch64
  (… one block per member; every member: ELF64 / AArch64)
```

Symbol evidence (`nm` on the archive):

```
$ nm libmetal_stub.a | grep -E 'MTLDevice'
00000000000004a0 T MTLDeviceGetCaps
0000000000000500 T MTLDeviceNewCommandQueue
0000000000000524 T MTLDeviceNewLibraryWithSource
000000000000041c T MTLDeviceRelease
0000000000000000 T MTLDeviceNewRenderPipelineStateWithDescriptor

$ nm libmetal_stub.a | grep -E 'MTLCommandQueue'
0000000000000080 T MTLCommandQueueCommandBuffer
000000000000005c T MTLCommandQueueRelease

$ nm libmetal_stub.a | grep -E 'MTLLibrary'
0000000000000770 T MTLLibraryNewFunctionWithName
0000000000000930 T MTLLibraryRelease
```

`MTLDevice`, `MTLCommandQueue`, and `MTLLibrary` symbols are all present and
exported as global text (`T`).

## Artifact

* Path (node scratch): `$DARLING_BUILD_DIR/ipa-metal-aarch64/libmetal_stub.a`
* Size: 38 968 bytes
* `sha256`: `832c7e4493925f39340bd8984c39edc23582389704dcda44a4da2b9ea7484807`
* Full build log: `$DARLING_BUILD_DIR/ipa-metal-aarch64/build.log`

## Note on `cargo check`

The `ipa-runtime` tree is a **CMake** project (top-level `CMakeLists.txt`);
there is no `ipa-runtime` root `Cargo.toml`.  Only `ipa-runtime/machotool` is a
workspace Cargo member (package name `machotool`).  The workspace manifest
(`$SRC_PARENT/Cargo.toml`) references `proto/broker`, `proto/app`, … which are
absent on this node, so `cargo check` cannot load the workspace here — the
stubs themselves are built with `clang`, not Cargo.
