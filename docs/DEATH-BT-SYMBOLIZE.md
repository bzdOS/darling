# Control #142 step 20 — death backtrace symbolized: each of the 5 frames = image + offset

**Date:** 2026-10-09
**Branch:** task/death-bt-symbolize
**Base:** pr-arm64 = 72d343212 (after the step-19 doc-size amend)

No run, no rebuild. The core from step 19 is reused.

## Core and executable

Core `/tmp/mldr-dead.core` (332 832 768 B, from step 19). The core header
names the executable it came from:

```
... from '$DARLING_BUILD_DIR/dserver/mldr-real/mldr
/tmp/darling-local-overlay/cft-fwmacho', pid=88069
```

The mldr binary at that path (731 720 B, mtime 07:37) predates the core
(08:35), so it is the image that produced the core.

## Method — why `image list` / `image lookup` alone do not answer

`lldb -batch -c /tmp/mldr-dead.core $DARLING_BUILD_DIR/dserver/mldr-real/mldr`
loads the core. `image list` lists only the **37 host FreeBSD ELF modules**
(mldr plus /lib, /usr/lib, /usr/local/lib); the **guest Mach-O images are not
listed** — lldb on FreeBSD does not load them. `image lookup -a <addr>` aborts
lldb for every one of the five frame addresses (the batch exits rc=1 with no
output), so no symbol can be taken from it.

Two substitutes were used:

1. **images** — the core carries the process vmmap as the FreeBSD
   `NT_PROCSTAT_VMMAP` note (type 0xa); `procstat -v /tmp/mldr-dead.core`
   prints every region with its file path. This names the guest Mach-O images.
2. **symbols** — `llvm-nm -n` on the mapped file (Mach-O); its addresses are
   `__TEXT`-relative and match the vmmap offset.

`image list` head/tail (host modules; base of mldr = 0x200000):

```
[  0] 4F878C33 0x0000000000200000 .../dserver/mldr-real/mldr
[  1] E9915BBA 0x00000008227ce000 /lib/libc.so.7
...
[ 36] 6F148EE0 0x00000e27fed5e000 /libexec/ld-elf.so.1
```

No guest image (nothing at 0xa6...) appears in this list.

## The vmmap regions the frames land in (`procstat -v`)

```
0x8216c3000   0x8216c4000    r-x  ... ph                                <- frame #2
0xa60cdf43000 0xa60cdfa2000  r-x  ... vn .../usr/lib/system/libsystem_kernel.dylib            <- #0,#1
0xa60cf1f7000 0xa60d1321000  r-x  ... vn .../Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework  <- #3,#4
```

## The five frames

| # | host address | image | image base | offset | symbol / instruction |
|---|---|---|---|---|---|
| 0 (rip) | 0xa60cdf6ef12 | libsystem_kernel.dylib | 0xa60cdf43000 | +0x2bf12 | `_state_to_kernel + 0x1a2` |
| 1 | 0xa60cdf6f347 | libsystem_kernel.dylib | 0xa60cdf43000 | +0x2c347 | `_sigexc_handler + 0xa7` |
| 2 | 0x8216c32d3 | FreeBSD sigcode / signal trampoline (phys page, no file) | 0x8216c3000 | +0x2d3 | `movq $0x1a1,%rax; syscall` (sigreturn) |
| 3 | 0xa60d0a5a3e8 | Google Chrome for Testing Framework | 0xa60cf1f7000 | +0x18633e8 | no symbol (stripped) |
| 4 | 0xa60d0a5987e | Google Chrome for Testing Framework | 0xa60cf1f7000 | +0x186287e | no symbol (stripped) |

**rip = frame #0 → image = `libsystem_kernel.dylib`** (guest overlay
`usr/lib/system/libsystem_kernel.dylib`).

Symbols (`llvm-nm -n` on the dylib): frame #0 lies in `_state_to_kernel`
(nearest preceding symbol, +0x1a2); frame #1 in `_sigexc_handler` (+0xa7).
The Chrome framework has only three defined text symbols (`_ChromeMain`
@0x3fe0, `_ChromeAppModeStart_v8`, `_ChromeWebAppShortcutCopierMain`), all
near the start; frames #3/#4 at +0x18633e8 / +0x186287e lie past them, so no
symbol. Frame #2 is the FreeBSD sigcode page (mapped physical, no file):
`leaq 0x10(%rsp),%rdi; pushq $0x0; movq $0x1a1,%rax; syscall` — a sigreturn
trampoline.

## The faulting instruction and registers

```
rip = 0x00000a60cdf6ef12   movzwl (%r15), %esi     ; read 2 bytes from *r15
r15 = 0x0000000000010002                            ; small, unmapped -> SIGSEGV
rsp = 0x000000000023fc10   mldr`_sigsys_altstack + 61456
rbp = 0x000000000023fc30   mldr`_sigsys_altstack + 61488
rbx = 0x000000000023fd24   mldr`_sigsys_altstack + 61732
r13 = 0x000000000023fd24   mldr`_sigsys_altstack + 61732
r14 = 0x000000000023ff80   mldr`_sigsys_altstack + 62336
```

The fault is a **data** fault: the faulting instruction sits at a valid guest
address and dereferences `r15 = 0x10002` (a small, unmapped value), not a bad
PC. Every stack pointer is inside **mldr's signal alternate stack**
(`_sigsys_altstack`).

## Verdict — the fork

**Frame #0 is in a guest image, not in mldr's own binary text.** The image is
the guest's `libsystem_kernel.dylib`, function `_state_to_kernel`, called from
`_sigexc_handler` — Darling's guest **signal-exception handler / resume path**
— running on mldr's sigaltstack. So the death is not an unhandled SIGSEGV of
arbitrary guest code at a PC the trap does not watch: the handler itself
dereferences a bad pointer (`r15 = 0x10002`) inside `_state_to_kernel`.

## Repro

```sh
sudo procstat -v /tmp/mldr-dead.core
sudo lldb -batch -c /tmp/mldr-dead.core $DARLING_BUILD_DIR/dserver/mldr-real/mldr \
  -o 'bt 5' -o 'register read rip rsp rbp r15' \
  -o 'disassemble --start-address 0xa60cdf6ef12 --count 12'
llvm-nm -n <overlay>/usr/lib/system/libsystem_kernel.dylib
```

Core `/tmp/mldr-dead.core` (332 832 768 B).
