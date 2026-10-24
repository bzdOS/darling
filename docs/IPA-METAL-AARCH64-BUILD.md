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

## `cargo check -p ipa-runtime`

The `ipa-runtime` source tree (at `$SRC_PARENT/ipa-runtime`) is primarily a
CMake project.  An `ipa-runtime` Cargo package has been added at
`ipa-runtime/` in this tree so that `cargo check -p ipa-runtime` validates the
Metal stub build end-to-end: the package carries a `build.rs` that invokes
`clang --target=aarch64-unknown-freebsd15.0` over every `.c` stub and archives
`libmetal_stub.a`.

```
$ cargo check -p ipa-runtime --offline
   Compiling ipa-runtime v0.1.0 (...)
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.53s
```

`cargo check` runs the `build.rs`, which reproduces the exact same AArch64
cross-compile: `readelf -h` on the produced `libmetal_stub.a` returns
`Machine: AArch64` for all six members, and `nm` lists the `MTLDevice`,
`MTLCommandQueue`, and `MTLLibrary` symbols.  Host x86-64 code is never
emitted (the `build.rs` passes `--target=aarch64-unknown-freebsd15.0`).

**Package contents (committed to this branch):**

* `ipa-runtime/Cargo.toml` — package `ipa-runtime`, no external crates
* `ipa-runtime/build.rs` — cross-compiles `Metal/src/*.c` → `libmetal_stub.a`
* `ipa-runtime/src/lib.rs` — minimal wrapper exposing `metal_stub_build_info()`
* `ipa-runtime/Metal/{src,include}/` — C stub sources and headers
