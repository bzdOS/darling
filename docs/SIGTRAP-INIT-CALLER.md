# Control #150 step 28 — the owner of the `int3` at Chrome+0x95D07C0 and its place in the initializer cascade

**Date:** 2026-10-09
**Branch:** task/sigtrap-init-caller
**Base:** pr-arm64 = 6dc3b603f (step 27 merged)
**Method:** static only — no run. Disassembly of the shipped framework file,
plus the `SIGTRAP-AUDIT-DELIVER` run of step 27 (`diag/sigexc-rip-2.log`).

All addresses below are **offsets into the Chrome framework image** (`__TEXT`
file offset == VA, image preferred base 0). The runtime address is
`__TEXT_base + offset`, with `__TEXT_base = 0x219528FF7000` in the step-27 run
(read from that run's own dyld output). Offsets and runtime addresses are both
given where it matters.

---

## 1. The window around the trap — function `Chrome+0x95D0780`

Step 27 fixed the fatal `rip` at `0x2195325c77c1` = `Chrome+0x95D07C1`. RIP is
one byte past the faulting instruction (x86 advances RIP over the 1-byte `int3`
before delivering `SIGTRAP`/`TRAP_BRKPT`), so the trap itself is at
**`Chrome+0x95D07C0`**.

Disassembly of the whole containing function, `Chrome+0x95D0780`:

```
95d0780: 55                pushq  %rbp
95d0781: 48 89 e5          movq   %rsp, %rbp
95d0784: 53                pushq  %rbx
95d0785: 48 83 ec 18       subq   $0x18, %rsp
95d0789: 48 8d 7d f4       leaq   -0xc(%rbp), %rdi
95d078d: c7 07 3f 00 00 00  movl   $0x3f, (%rdi)          ; local = 0x3f
95d0793: e8 78 5e a3 f6    callq  0x6610                 ; shared stub (push/mov/pop/ret)
95d0798: e8 c3 ff ff ff    callq  0x95d0760              ; eax = global int @0xfea64c8
95d079d: 0f 57 c0          xorps  %xmm0, %xmm0
95d07a0: 48 8d 5d e0       leaq   -0x20(%rbp), %rbx
95d07a4: 0f 29 03          movaps %xmm0, (%rbx)           ; zero 16 bytes
95d07a7: 89 c2             movl   %eax, %edx
95d07a9: 48 8d 35 d0 8f 26 05  leaq 0x5268fd0(%rip), %rsi  ; -> 0xe839780 = "error"
95d07b0: 48 89 df          movq   %rbx, %rdi
95d07b3: e8 38 f9 ff ff    callq  0x95d00f0              ; copy "error" into the buffer
95d07b8: 48 89 df          movq   %rbx, %rdi
95d07bb: e8 50 5e a3 f6    callq  0x6610                 ; shared stub
95d07c0: cc                int3                          ; <-- THE FATAL TRAP
95d07c1: 0f 0b             ud2
95d07c3: 66 66 66 66 2e 0f 1f 84 00 00 00 00 00  nopw %cs:(%rax,%rax)
```

**Condition before the trap: none in this window.** There is no conditional
branch and no comparison in `0x95D0780`–`0x95D07C2`; control reaches the `int3`
by falling straight through. The `int3; ud2` pair is the standard encoding of
`__builtin_trap()` (Clang emits `int3; ud2` for a noreturn trap; `ud2` alone for
`__builtin_unreachable`). So `Chrome+0x95D0780` is **not a conditional assert
site — it is a small crash routine whose body ends in an unconditional
`__builtin_trap()`**. The routine formats a fixed message (the string "error" at
`0xe839780` plus a global integer read from `0xfea64c8`) and then traps.

The decision that enters this routine is therefore **at its caller**, not here.

## 2. The single caller — `Chrome+0x18633E3`

A byte scan of the whole image (every `e8`/`e9` relative branch) finds:

- **exactly one call to `Chrome+0x95D0780`**, from **`Chrome+0x18633E3`**;
- **no direct branch of any kind to `Chrome+0x95D07C0`**.

So the only path to the trap is: call `0x95D0780` from `0x18633E3`, then fall
through its body to the `int3`.

That call site sits in the cold path of the function `Chrome+0x1863180`, a
call-once address-pool initializer. The relevant code:

```
1863259: 4c 89 2d e8 34 80 0e   movq   %r13, 0xe8034e8(%rip)   ; @0x10066748
1863260: c7 04 24 ff ff ff ff   movl   $0xffffffff, (%rsp)
1863267: 48 ba 00 00 00 00 08 00 00 00  movabsq $0x800000000, %rdx   ; 32 GiB
1863271: 31 ff                  xorl   %edi, %edi
1863273: 48 89 d6               movq   %rdx, %rsi              ; size = 32 GiB
1863276: 31 c9                  xorl   %ecx, %ecx
1863278: 45 31 c0               xorl   %r8d, %r8d
186327b: 41 b9 fd 00 00 00      movl   $0xfd, %r9d
1863281: e8 da 0c e8 ff         callq  0x16e3f60               ; Mmap wrapper
1863286: 48 89 05 73 fd 57 0e   movq   %rax, 0xe57fd73(%rip)   ; @0xfde3000
186328d: 48 85 c0               testq  %rax, %rax
1863290: 0f 84 4d 01 00 00      je     0x18633e3               ; NULL -> crash
...
186331d: e8 3e 0c e8 ff         callq  0x16e3f60               ; second reservation
1863322: 48 85 c0               testq  %rax, %rax
1863325: 0f 84 b8 00 00 00      je     0x18633e3               ; NULL -> crash
...
18633e3: e8 98 d3 d6 07         callq  0x95d0780               ; crash routine
18633e8: cc                     int3
18633e9: 0f 0b                  ud2
```

`0x16e3f60` is a `mmap` wrapper (builds prot/flags, falls through to the raw
`mmap` at `0x16e4240`; returns 0 on failure). The reservation it is asked for
here is **`0x800000000` = 32 GiB** (and a second one of `0x400000000` = 16 GiB
just below).

**Condition before the trap (at the caller):** the 32 GiB address-space
reservation returned **NULL** (`testq %rax,%rax; je 0x18633E3`). That NULL path
calls the crash routine `0x95D0780`, which traps. The other cold edge in the
same function (`ja 0x18633E8`, if a table size > 8) jumps straight to the
`int3` at `0x18633E8` — but that would leave RIP at `0x18633E9`, and the observed
RIP is `0x95D07C1`, so the run went through the **reservation-NULL** edge.

## 3. Place in the initializer cascade (from the step-27 log)

The last initializer dyld calls before the trap is, in
`diag/sigexc-rip-2.log` (line 1292539):

```
dyld: calling initializer function 0x21952b1214c0 in /Frameworks/Google Chrome for Testing Framework...
```

`0x21952b1214c0 = Chrome+0x212A4C0`. Static call-graph walk from it to the trap:

| # | offset | runtime (base+off) | how it is reached |
|---|--------|--------------------|-------------------|
| 0 | `0x212A4C0` | `0x21952b1214c0` | last dyld initializer (log line 1292539); searches the config table for a 14-char string ending `ionAlloc` (`movabsq "ionAlloc"` at `0x212a526`, `strlen==0xe` at `0x212a545`) — i.e. **PartitionAlloc** |
| 1 | `0x212A8C0` | `0x21952b1218c0` | `callq` at `0x212a562` inside the initializer |
| 2 | `0x212AA00` | `0x21952b121a00` | `callq` at `0x212a9cd` (ret `0x212A9D2` = the cascade address `+0x212a9d2`) |
| 3 | `0x1862820` | `0x21952a859820` | `callq` at `0x212ab8c` inside `0x212AA00` (the `+0x212ab3f` cascade address is the ret of the sibling `callq 0xccdb60` at `0x212ab3a`, same function) |
| 4 | `0x1863180` | `0x21952a85a180` | `callq` at `0x1862879` inside `0x1862820` — the call-once pool initializer |
| 5 | `0x16E3F60` | `0x21952a6daf60` | `callq` at `0x1863281` / `0x186331d` — the mmap wrapper, 32 GiB |
| — | `0x18633E3` | `0x21952a85a3e3` | NULL edge `je` after step 5 |
| 6 | `0x95D0780` | `0x2195325c7780` | `callq` at `0x18633e3` — crash routine |
| 7 | `0x95D07C0` | `0x2195325c77c0` | `int3` — **the fatal trap** (RIP `…c77c1` one byte past) |

So the trap's place in the cascade is: the Chrome initializer `+0x212A4C0`
(PartitionAlloc setup) → `+0x212A8C0` → `+0x212AA00` → `+0x1862820` →
`+0x1863180` (address-pool call-once init) → **a 32 GiB `mmap` reservation
returns NULL** → crash routine `+0x95D0780` → `int3`.

## 4. Verdict

The fatal `int3` at `Chrome+0x95D07C0` is a **deliberate `__builtin_trap()`
(`int3; ud2`) inside the allocator's crash routine `Chrome+0x95D0780`**, reached
because the **32 GiB address-space reservation returned NULL** in the pool
initializer `Chrome+0x1863180` during the PartitionAlloc phase of Chrome's own
initializer `Chrome+0x212A4C0`. It is not a stray byte, not an mldr-injected
`int3`, and not a conditional `assert` at the trap site itself — the trap is the
OOM/failed-reservation path of the guest's own allocator, and the "condition"
lives at the caller (`Chrome+0x1863290` / `+0x1863325`).

This closes step 27's open question ("who raised the SIGTRAP"): Chrome's own
allocator did, via its reservation-failure crash path, during the PartitionAlloc
initializer.

## 5. Honest limits

- Function names are **stripped**; identities are inferred from the code and the
  string table. "PartitionAlloc" is inferred from the `movabsq "ionAlloc"` +
  `strlen==14` search in the initializer; "mmap wrapper" from the prot/flags
  construction and the raw `mmap` at `0x16e4240`. These are the nearest anchors,
  not symbol-table names.
- The exact string consumed at the trap site is `"error"` (`0xe839780`); the
  `"PERFETTO_CHECK(...)"` strings nearby (`0xe839795…`) belong to the same string
  table but are not the pointer this routine loads.
- Only **direct** branches were scanned; an indirect call into `0x95D0780` was
  not observed and is not expected for a cold crash routine, but was not
  exhaustively excluded.

## Repro (static only — no run)

```sh
CHROME_BIN="$DARLING_OVERLAY/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework"

# 1. the window around the trap
llvm-objdump -d --start-address=0x95d0780 --stop-address=0x95d07d0 "$CHROME_BIN"
#   95d07c0: cc  int3

# 2. the single caller (whole-image scan for e8/e9 -> 0x95d0780)
#   -> only 0x18633e3;  no branch to 0x95d07c0
python3 - "$CHROME_BIN" <<'PY'
import sys,struct
d=open(sys.argv[1],'rb').read()
for i in range(len(d)-5):
    if d[i] in (0xe8,0xe9) and i+5+struct.unpack_from('<i',d,i+1)[0]==0x95d0780:
        print('call/jmp from', hex(i))
PY

# 3. the condition at the caller
llvm-objdump -d --start-address=0x1863259 --stop-address=0x18633ea "$CHROME_BIN"

# 4. the initializer cascade (last initializer before the trap)
grep 'calling initializer' "$DARLING_BUILD_DIR"/../diag/sigexc-rip-2.log | tail -1
#   -> 0x21952b1214c0  =  Chrome+0x212A4C0
```
