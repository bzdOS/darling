#!/usr/bin/env python3
"""Binary patch for a built libsystem_malloc.dylib: make
set_flags_from_environment tolerate a NULL environ.

Why: on the FreeBSD port malloc initializes before libSystem's
_program_vars_init sets _NSGetEnviron(), so *_NSGetEnviron() is NULL and
set_flags_from_environment (src/external/libmalloc/src/malloc.c:1056)
dereferences it.  The patch makes the function skip environment parsing when
environ is NULL (leaving the default malloc flags), then return.

The image is a thin x86_64 Mach-O whose __TEXT has vmaddr 0 and fileoff 0, so
file offset == virtual address.  Two in-place edits, no file growth, no load
command moves:

  vaddr 0x325e0, 7 bytes
    48 8b 00 48 89 45 f0        movq (%rax),%rax ; movq %rax,-0x10(%rbp)
  ->e9 <rel32> 90 90            jmp 0x808 ; nop ; nop

  vaddr 0x808, 42 bytes (all-zero padding right after the load commands,
  inside __TEXT which is r.x):
    48 85 c0                    testq %rax,%rax
    75 0f                       jne .Lnormal
    c7 05 <rel32> 00 01 00 00   movl $0x100,_malloc_debug_flags   ; NULL case
    e9 <rel32>                  jmp epilogue (return)
  .Lnormal:
    48 8b 00                    movq (%rax),%rax
    48 89 45 f0                 movq %rax,-0x10(%rbp)
    c7 05 <rel32> 00 01 00 00   movl $0x100,_malloc_debug_flags
    e9 <rel32>                  jmp 0x325f1 (continue)

0x100 is MALLOC_ABORT_ON_CORRUPTION, the __LP64__ default the original sets.

Usage: patch-malloc-env-guard.py <path-to-libsystem_malloc.dylib>
Expected patched sha256:
  ae522db748dcf6befd3edf8a32b017cd1a67b83effe99f0bbf9ba6d93ce055a5
"""
import sys
import hashlib

MAIN = 0x325E0
MAIN_ORIG = bytes.fromhex("48 8b 00 48 89 45 f0")
CAVE = 0x808
EPILOGUE = 0x32FB3          # addq $0x90,%rsp ; popq %rbp ; retq
CONTINUE = 0x325F1          # movq -0x10(%rbp),%rax
FLAGS = 0x54028             # _malloc_debug_flags
EXPECTED_SHA256 = "ae522db748dcf6befd3edf8a32b017cd1a67b83effe99f0bbf9ba6d93ce055a5"


def rel32(next_instr, target):
    return (target - next_instr).to_bytes(4, "little", signed=True)


def build_cave():
    code = bytearray()
    code += bytes.fromhex("48 85 c0")                       # testq %rax,%rax
    code += bytes.fromhex("75 0f")                          # jne .Lnormal
    # NULL case: default flags, then return
    movl = CAVE + len(code)
    code += bytes.fromhex("c7 05") + rel32(movl + 6, FLAGS) + bytes.fromhex("00 01 00 00")
    jmp = CAVE + len(code)
    code += bytes.fromhex("e9") + rel32(jmp + 5, EPILOGUE)
    # normal case
    code += bytes.fromhex("48 8b 00")                       # movq (%rax),%rax
    code += bytes.fromhex("48 89 45 f0")                    # movq %rax,-0x10(%rbp)
    movl = CAVE + len(code)
    code += bytes.fromhex("c7 05") + rel32(movl + 6, FLAGS) + bytes.fromhex("00 01 00 00")
    jmp = CAVE + len(code)
    code += bytes.fromhex("e9") + rel32(jmp + 5, CONTINUE)
    return bytes(code)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    data = bytearray(open(path, "rb").read())
    if data[MAIN:MAIN + 7] != MAIN_ORIG:
        sys.exit("unexpected bytes at 0x%x: %s" % (MAIN, data[MAIN:MAIN + 7].hex(" ")))
    code = build_cave()
    if any(data[CAVE:CAVE + len(code)]):
        sys.exit("cave 0x%x is not zero" % CAVE)
    data[MAIN:MAIN + 7] = bytes.fromhex("e9") + rel32(MAIN + 5, CAVE) + bytes.fromhex("90 90")
    data[CAVE:CAVE + len(code)] = code
    open(path, "wb").write(data)
    digest = hashlib.sha256(data).hexdigest()
    print("patched %s sha256 %s" % (path, digest))
    if digest != EXPECTED_SHA256:
        print("WARNING: sha256 differs from the recorded one", file=sys.stderr)


if __name__ == "__main__":
    main()
