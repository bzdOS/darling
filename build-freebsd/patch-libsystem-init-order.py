#!/usr/bin/env python3
"""patch-libsystem-init-order.py — run _libc_initializer before __malloc_init
inside libSystem.B.dylib's _libSystem_initializer, without rebuilding anything.

Why: on the FreeBSD port libSystem_initializer calls __malloc_init (libsystem_malloc)
before _libc_initializer (libsystem_c). _program_vars_init runs only from
_libc_initializer and is what sets libsystem_c's local _environ_pointer, so every
early malloc reader (set_flags_from_environment, __malloc_initialize/
nano_common_init) sees a NULL environ. This patch moves the _libc_initializer
call ahead of __malloc_init and NOPs the original call, so it runs exactly once.

Pre-conditions measured on the stock image (thin x86_64, __TEXT vmaddr 0 ==
file offset 0, so vaddr == file offset):
  _libSystem_initializer at 0x660 (local);
  0x6c8  movq %rbx,%rdi                 (rdi = apple)
  0x6cb  e8 04 06 00 00  callq 0xcd4    (stub __malloc_init)
  0x6fb  leaq 0x2c16(%rip),%rdi         (rdi = libc_funcs @ 0x3318)
  0x702  movq %r15,%rsi                 (envp)
  0x705  movq %rbx,%rdx                 (apple)
  0x708  movq %r14,%rcx                 (vars)
  0x70b  e8 12 06 00 00  callq 0xd22    (stub _libc_initializer)
  0xcd4  jmpq *0x3048                    (stub __malloc_init)
  0xd22  jmpq *0x30b0                    (stub _libc_initializer)
  0x2220..0x3000  zero padding inside __TEXT (r.x)

Patch:
  0x6cb, 5 bytes: e8 04 06 00 00 -> e9 <rel32 to 0x2220>
  0x70b, 5 bytes: e8 12 06 00 00 -> 90 90 90 90 90
  0x2220, 34 bytes (cave):
    lea 0x3318(%rip),%rdi ; mov %r15,%rsi ; mov %rbx,%rdx ; mov %r14,%rcx
    callq 0xd22           ; _libc_initializer(libc_funcs, envp, apple, vars)
    mov %rbx,%rdi         ; rdi = apple
    callq 0xcd4           ; __malloc_init(apple)
    jmp 0x6d0             ; continue after the original call
  Stack: rsp%16==0 at the call site (5 pushes + sub 0x30 from a %16==8 entry),
  so both callq are ABI-aligned with no extra adjustment; rbx/r14/r15 are
  callee-saved and preserved across _libc_initializer.

Usage: patch-libsystem-init-order.py <path-to-libSystem.B.dylib>
"""
import sys
import hashlib

CALL_MALLOC = 0x6CB
CALL_MALLOC_ORIG = bytes.fromhex("e8 04 06 00 00")
CALL_LIBC = 0x70B
CALL_LIBC_ORIG = bytes.fromhex("e8 12 06 00 00")
CAVE = 0x2220
CAVE_END = 0x3000
LIBC_FUNCS = 0x3318
STUB_LIBC_INIT = 0xD22
STUB_MALLOC_INIT = 0xCD4
JMP_BACK = 0x6D0


def rel32(next_instr, target):
    return (target - next_instr).to_bytes(4, "little", signed=True)


def build_cave():
    code = bytearray()
    # _libc_initializer(libc_funcs, envp, apple, vars)
    code += bytes.fromhex("48 8d 3d") + rel32(CAVE + len(code) + 7, LIBC_FUNCS)   # lea
    code += bytes.fromhex("4c 89 fe")   # mov %r15,%rsi
    code += bytes.fromhex("48 89 da")   # mov %rbx,%rdx
    code += bytes.fromhex("4c 89 f1")   # mov %r14,%rcx
    code += bytes.fromhex("e8") + rel32(CAVE + len(code) + 5, STUB_LIBC_INIT)     # call
    # __malloc_init(apple)
    code += bytes.fromhex("48 89 df")   # mov %rbx,%rdi
    code += bytes.fromhex("e8") + rel32(CAVE + len(code) + 5, STUB_MALLOC_INIT)   # call
    # continue after the original call
    code += bytes.fromhex("e9") + rel32(CAVE + len(code) + 5, JMP_BACK)           # jmp
    return bytes(code)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    data = bytearray(open(path, "rb").read())
    if data[CALL_MALLOC:CALL_MALLOC + 5] != CALL_MALLOC_ORIG:
        sys.exit("unexpected bytes at 0x%x: %s" % (CALL_MALLOC, data[CALL_MALLOC:CALL_MALLOC + 5].hex(" ")))
    if data[CALL_LIBC:CALL_LIBC + 5] != CALL_LIBC_ORIG:
        sys.exit("unexpected bytes at 0x%x: %s" % (CALL_LIBC, data[CALL_LIBC:CALL_LIBC + 5].hex(" ")))
    code = build_cave()
    if CAVE + len(code) > CAVE_END:
        sys.exit("cave does not fit")
    if any(data[CAVE:CAVE + len(code)]):
        sys.exit("cave 0x%x is not zero" % CAVE)
    data[CALL_MALLOC:CALL_MALLOC + 5] = bytes.fromhex("e9") + rel32(CALL_MALLOC + 5, CAVE)
    data[CALL_LIBC:CALL_LIBC + 5] = bytes.fromhex("90 90 90 90 90")
    data[CAVE:CAVE + len(code)] = code
    open(path, "wb").write(data)
    print("patched %s (cave %d bytes) sha256 %s" % (path, len(code), hashlib.sha256(data).hexdigest()))


if __name__ == "__main__":
    main()
