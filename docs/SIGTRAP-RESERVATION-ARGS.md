# Control #151 step 29 — the two address-space reservations and the guest `mmap` path

**Date:** 2026-10-09
**Branch:** task/reservation-null-args
**Base:** pr-arm64 = d692b5344 (step 28 merged)
**Method:** static only — disassembly of the shipped framework file + a walk of
the guest `mmap` path in this tree. No run.

Offsets are into the Chrome framework image (`__TEXT` file offset == VA, image
base 0). Runtime address = `__TEXT_base + offset`; in the step-27 run
`__TEXT_base = 0x219528FF7000`.

---

## 0. Summary

Both reservations come from the call-once pool initializer `Chrome+0x1863180`
(step 28) and both call the same wrapper `Chrome+0x16E3F60`, which resolves to
the guest `mmap` (the stub it calls is `libsystem_kernel.dylib:_mmap`, confirmed
from the Mach-O indirect symbol table). Both requests are
**`addr = 0`, `prot = PROT_NONE`, `flags = MAP_PRIVATE|MAP_ANON`, `offset = 0`**;
they differ only in **length and alignment** (32 GiB / 32 GiB, then 16 GiB /
page size). On the Darling side the guest `mmap` is handled in **one place** —
`src/startup/mldr/freebsd_syscall_trap.c:641-643` — which forwards the args
**unchanged** to FreeBSD `SYS_mmap`. There is **no branch in this tree that
fails for these flags**; the failing return is the FreeBSD kernel's `mmap`. The
nearest Darling anchors are named in §3-4.

## 1. The two call sites — bytes and register decode

### First reservation — `Chrome+0x1863281` (runtime `0x21952a85a281`)

```
1863259: 4c 89 2d e8 34 80 0e       movq    %r13, 0xe8034e8(%rip)   ; global, not an arg
1863260: c7 04 24 ff ff ff ff       movl    $0xffffffff, (%rsp)     ; 7th arg (stack)
1863267: 48 ba 00 00 00 00 08 00 00 00  movabsq $0x800000000, %rdx   ; 0x800000000 = 32 GiB
1863271: 31 ff                      xorl    %edi, %edi              ; rdi = 0
1863273: 48 89 d6                   movq    %rdx, %rsi              ; rsi = 32 GiB
1863276: 31 c9                      xorl    %ecx, %ecx              ; rcx = 0
1863278: 45 31 c0                   xorl    %r8d, %r8d              ; r8 = 0
186327b: 41 b9 fd 00 00 00          movl    $0xfd, %r9d             ; r9 = 0xfd
1863281: e8 da 0c e8 ff             callq   0x16e3f60
```

### Second reservation — `Chrome+0x186331D` (runtime `0x21952a85a31d`)

```
1863296: 48 bb 00 00 00 00 04 00 00 00  movabsq $0x400000000, %rbx   ; 0x400000000 = 16 GiB
...
18632fc: 4c 8b 35 15 fe 93 0d       movq    0xd93fe15(%rip), %r14   ; r14 = &page_size (0xf1a3118)
1863303: 49 8b 16                   movq    (%r14), %rdx            ; rdx = page size (0x1000/0x4000)
1863306: c7 04 24 ff ff ff ff       movl    $0xffffffff, (%rsp)     ; 7th arg (stack)
186330d: 31 ff                      xorl    %edi, %edi              ; rdi = 0
186330f: 48 89 de                   movq    %rbx, %rsi              ; rsi = 16 GiB
1863312: 31 c9                      xorl    %ecx, %ecx              ; rcx = 0
1863314: 45 31 c0                   xorl    %r8d, %r8d              ; r8 = 0
1863317: 41 b9 fd 00 00 00          movl    $0xfd, %r9d             ; r9 = 0xfd
186331d: e8 3e 0c e8 ff             callq   0x16e3f60
```

### Registers at the call

| reg | first | second | role, read off the wrapper body (§2) |
|-----|-------|--------|--------------------------------------|
| `rdi` | `0` | `0` | `mmap` addr hint |
| `rsi` | `0x800000000` (32 GiB) | `0x400000000` (16 GiB) | `mmap` length |
| `rdx` | `0x800000000` (32 GiB) | page size | **alignment** (`addr &= -rdx`) |
| `rcx` | `0` | `0` | offset added after alignment |
| `r8` | `0` | `0` | prot index into the table at `0xe0e8c50` |
| `r9` | `0xfd` | `0xfd` | tag byte packed into the flags (`<<24`) |
| `[rsp]` | `0xffffffff` | `0xffffffff` | fd / flags override (`-1`) |

## 2. The wrapper `Chrome+0x16E3F60` → the `mmap` it actually makes

The wrapper is a **7-argument** reservation routine (not a plain `mmap`): it
aligns the hint and then calls the raw `mmap` at `Chrome+0x16E4240`.

```
16e3f85: 48 85 ff             testq  %rdi, %rdi
16e3f88: 75 14                jne    0x16e3f9e
16e3f8a: 4c 89 e3             movq   %r12, %rbx          ; rbx = alignment (rdx)
16e3f8d: 48 f7 db             negq   %rbx
16e3f90: e8 2b ae 20 ff       callq  0x8eedc0            ; randomized hint (~1 TB; see below)
16e3f95: 49 89 c6             movq   %rax, %r14
16e3f98: 49 21 de             andq   %rbx, %r14          ; addr &= -alignment
16e3f9b: 4d 01 fe             addq   %r15, %r14          ; addr += offset (rcx)
16e3f9e: 4c 89 f7             movq   %r14, %rdi          ; mmap arg1 = addr
16e3fa1: 4c 89 ee             movq   %r13, %rsi          ; mmap arg2 = len
16e3fa4: 8b 55 d4             movl   -0x2c(%rbp), %edx   ; = r8  -> prot index
16e3fa7: 8b 4d d0             movl   -0x30(%rbp), %ecx   ; = r9  -> flags high byte
16e3faa: 44 8b 45 10          movl   0x10(%rbp), %r8d    ; = [rsp] -> fd / override
16e3fae: e8 8d 02 00 00       callq  0x16e4240
```

`0x16e4240` translates and calls `mmap`:

```
16e424e: 89 cb                movl   %ecx, %ebx          ; ebx = r9 (0xfd)
16e4250: c1 e3 18             shll   $0x18, %ebx         ; ebx = 0xfd << 24 = 0xfd000000
16e4253: 41 83 f8 ff          cmpl   $-0x1, %r8d         ; [rsp] == -1 ?
16e4257: 41 0f 45 d8          cmovnel %r8d, %ebx         ; not taken (== -1) -> keep 0xfd000000
16e425b: 83 fa 09             cmpl   $0x9, %edx
16e425e: 0f 83 e0 00 00 00    jae    0x16e4344           ; prot index >= 9 -> other path
16e426f: 48 8d 0d da 49 a0 0c leaq   0xe0e8c50(%rip), %rcx   ; prot table
16e4276: 44 0f b6 2c 08       movzbl (%rax,%rcx), %r13d   ; prot = table[edx]
16e4295: c1 e1 0b             shll   $0xb, %ecx
16e4298: 81 c9 02 10 00 00    orl    $0x1002, %ecx       ; flags = (..)|MAP_PRIVATE|MAP_ANON
16e429e: 4c 89 ff             movq   %r15, %rdi          ; mmap arg1 = addr
16e42a1: 4c 89 f6             movq   %r14, %rsi          ; mmap arg2 = len
16e42a4: 44 89 ea             movl   %r13d, %edx         ; mmap arg3 = prot
16e42a7: 41 89 d8             movl   %ebx, %r8d          ; mmap arg5 = fd = 0xfd000000
16e42aa: 45 31 c9             xorl   %r9d, %r9d          ; mmap arg6 = offset = 0
16e42ad: e8 8e ae 4d 0c       callq  0xdbbf140           ; = libsystem_kernel _mmap (see below)
16e42b2: 48 83 f8 ff          cmpq   $-0x1, %rax
16e42b6: 0f 84 8b 00 00 00    je     0x16e4347           ; MAP_FAILED -> error path
```

The prot table at `0xe0e8c50` is `00 00 01 03 03 05 05 07 07`; index `0`
(the reservation's `r8 = 0`) → **`0x00` = `PROT_NONE`**.

The stub `0xdbbf140` binds to **`_mmap`** (Mach-O `__stubs` reserved1 = 2581,
indirect-symbol index 2581+80 = 2661 → `_mmap`; the log confirms
`libsystem_kernel.dylib:_mmap`). So the call is a genuine `mmap`.

### The `mmap` arguments the wrapper produces

| arg | first reservation | second reservation |
|-----|-------------------|--------------------|
| addr | `hint & -0x800000000` (32 GiB-aligned, ≈ 1 TB) | `hint & -page_size` (≈ 1 TB) |
| len | `0x800000000` = **32 GiB** | `0x400000000` = **16 GiB** |
| prot | `0` = **PROT_NONE** | `0` = **PROT_NONE** |
| flags | `0x1002` = **MAP_PRIVATE\|MAP_ANON** | `0x1002` = **MAP_PRIVATE\|MAP_ANON** |
| fd | `0xfd000000` (packed tag; `MAP_ANON` → ignored by the kernel) | same |
| offset | `0` | `0` |

The `hint` comes from `Chrome+0x8EEDC0`, which builds a randomized base around
`1 << 40` (1 TB): `(rand & 0x3fffffffff) | (page_mask & 0x10000000000)`.

> Note on `fd`: the wrapper packs its 6th arg (`r9 = 0xfd`) into the high byte of
> the 5th `mmap` arg (`0xfd << 24`). With `MAP_ANON` set the kernel ignores `fd`,
> so this value is not the failure cause; it is reported here because it is what
> the disassembly computes. The 7-arg wrapper is a PartitionAlloc-style
> reservation routine (alignment + tag), not a plain `mmap`, so the arg *names*
> are inferred from the body, not from a symbol table (the image is stripped).

## 3. The guest `mmap` path in this tree — `named function:line`

The guest `mmap` (macOS BSD syscall 197) is trapped by mldr's SIGSYS handler and
handled in exactly one place:

```
src/startup/mldr/freebsd_syscall_trap.c
  562: static long dispatch_macos_bsd_syscall(unsigned int macos_nr, ...)
  641:     case MACOS_SYS_mmap:
  642:         /* macOS mmap args: addr, len, prot, flags, fd, offset — same layout */
  643:         return freebsd_raw_syscall(SYS_mmap, a1, a2, a3, a4, a5, a6);
```

- The translator is **`dispatch_macos_bsd_syscall()`** (`freebsd_syscall_trap.c:562`),
  mmap case at **`:641-643`**.
- The raw executor is **`freebsd_raw_syscall()`** (`freebsd_syscall_trap.c:412`):
  it issues the FreeBSD `syscall` and returns the kernel value, or `-errno` when
  the carry flag is set (`:422-430`).

Crucially, the **macOS** branch forwards `prot`/`flags` **unchanged** — it does
**not** translate them, unlike the Linux branch (added for the upstream dyld
overlay, `:1704-1718`, which remaps `MAP_ANONYMOUS 0x20` → `MAP_ANON 0x1000`
etc.). For this reservation that difference does not matter: the Darwin values
used here (`PROT_NONE 0`, `MAP_PRIVATE 0x2`, `MAP_ANON 0x1000`) are numerically
identical on FreeBSD.

## 4. Honest finding — where the failure comes from

There is **no branch in this tree that returns failure for these flags**. The
macOS mmap case (`:643`) blindly forwards to FreeBSD `SYS_mmap`; the failure
value (`MAP_FAILED` / `-errno`) is produced by the **FreeBSD kernel's `mmap`**,
which is outside this repository. The nearest Darling anchors that carry the
call and the failure are:

- `src/startup/mldr/freebsd_syscall_trap.c:643` — the untranslated forward;
- `src/startup/mldr/freebsd_syscall_trap.c:412` — `freebsd_raw_syscall`, which
  returns `-errno` on the carry flag (`:425-428`).

`freebsd_raw_syscall` has no size/flag guard, and the SIGSYS handler
(`:2960-3016`) has no mmap-specific check. So a Darling-side "branch that fails
for these flags" does **not** exist to be named — the honest answer is the
negative above.

**Hypothesis for the failure itself (not measured — no run in this step):** a
**32 GiB** `MAP_PRIVATE|MAP_ANON` reservation with a 32 GiB alignment and **no
`MAP_NORESERVE`** is the unusual request; if FreeBSD reserves backing for it,
the host's 8 GB RAM + 17 GB swap is far below 32 GiB. The second request (16 GiB)
is smaller but still over that budget. This is a guess about the FreeBSD kernel,
not a located branch, and is offered as such. A run that logs the guest's
`mmap` return errno (or a `truss`/`ktrace` of the reservation) would settle it.

## Repro (static only)

```sh
CHROME_BIN="$DARLING_OVERLAY/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework"

# both reservation call sites (args in registers + [rsp])
llvm-objdump -d --start-address=0x1863259 --stop-address=0x1863286 "$CHROME_BIN"
llvm-objdump -d --start-address=0x18632fc --stop-address=0x1863325 "$CHROME_BIN"

# the wrapper -> raw mmap -> the _mmap stub
llvm-objdump -d --start-address=0x16e3f60 --stop-address=0x16e3fc0 "$CHROME_BIN"
llvm-objdump -d --start-address=0x16e4240 --stop-address=0x16e42c0 "$CHROME_BIN"

# prot table (index 0 = PROT_NONE) and the packed tag byte 0xfd
python3 -c "d=open('$CHROME_BIN','rb').read(); print(' '.join(f'{b:02x}' for b in d[0xe0e8c50:0xe0e8c50+16]))"

# the guest mmap path in this tree
grep -n 'MACOS_SYS_mmap' src/startup/mldr/freebsd_syscall_trap.c     # :641-643
grep -n 'freebsd_raw_syscall(long' src/startup/mldr/freebsd_syscall_trap.c   # :412
```
