# LD64-CRASH-INPUT - the cctools ld64 SIGILL comes from the first relocatable atom

Control #142 follow-up. DIAGNOSTIC ONLY - ld64 is not fixed here.

## Reproduce
  export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
  export DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=...
  sh build-freebsd/narrow-build-target.sh src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
  -> cc: error: unable to execute command: Illegal instruction (core dumped)   (SIGILL, rc=132)

## The crashing atom's file (lldb on the core)
frame #0 = mach_o::relocatable::Atom<x86_64>::fixupsEnd() + 52; $rdi = the atom.
  atom +0x8          -> _section
  section +0x18      -> the File
  File +0x8          -> File::_path (first member of ld::File)
  x/s that pointer:
  "src/external/xnu/darling/src/libsystem_kernel/libsyscall/CMakeFiles/libsyscall_32.dir/mach/clock-i386-User.c.o"

## Crash site: Atom<x86_64>::fixupsEnd()
disassembly (frame #0, +52 is the trap):
  0x353fe4 <+4>:  movq   0x8(%rdi), %rax        ; rax = atom->_section
  0x353fe8 <+8>:  movl   $0xffffff, %ecx
  0x353fed <+13>: andl   0x4b(%rdi), %ecx       ; ecx = atom->_fixupsCount
  0x353ff0 <+16>: movq   0x18(%rax), %rdx       ; rdx = section->file()
  0x353ff4 <+20>: addl   0x40(%rdi), %ecx       ; ecx += atom->_fixupsStartIndex
  0x353ff7 <+23>: movq   0x58(%rdx), %rax       ; rax = file->_fixups.begin
  0x353ffb <+27>: movq   0x60(%rdx), %rdx       ; rdx = file->_fixups.end
  0x353fff <+31>: subq   %rax, %rdx
  0x354002 <+34>: sarq   $0x4, %rdx             ; rdx = file->_fixups.size()
  0x354006 <+38>: cmpq   %rcx, %rdx
  0x354009 <+41>: jbe    0x354014               ; if (size <= start+count) -> ud2
  0x35400b <+43>: shlq   $0x4, %rcx
  0x35400f <+47>: addq   %rcx, %rax             ; return &_fixups[start+count]
  0x354013 <+51>: retq
  0x354014 <+52>: ud2
So fixupsEnd() is compiled as `&_fixups[_fixupsStartIndex + _fixupsCount]` (the
non-DARLING form) with a bounds check `if (_fixups.size() <= start+count) trap`.
For this atom start+count = 10 + 11 = 21 and file->_fixups.size() = 21, so the
check traps on the legitimate one-past-the-end index. The DARLING form
(`machofile()._fixups.data() + ...`, macho_relocatable_file.cpp) exists exactly to
avoid the operator[] assertion; this binary does not use it.

## Bisection of the -filelist (895 paths; the non-.o inputs kept in every run)
baseline all-objects rc=132
A=446 of 891 -> CRASH rc=132
A=223 of 446 -> CRASH rc=132
A=112 of 223 -> CRASH rc=132
A=56  of 112 -> CRASH rc=132
A=28  of 56  -> CRASH rc=132
A=14  of 28  -> CRASH rc=132
A=7   of 14  -> CRASH rc=132
A=4   of 7   -> CRASH rc=132
A=2   of 4   -> CRASH rc=132
A=1   of 2   -> CRASH rc=132
CRASHING_OBJECT=src/external/xnu/darling/src/libsystem_kernel/libsyscall/CMakeFiles/libsyscall_32.dir/mach/clock-i386-User.c.o

## It is the FIRST relocatable atom, not a property of that .o
Swapping the first two objects in the -filelist moves the crash:
  filelist[0]=clock_priv-i386-User.c.o, filelist[1]=clock-i386-User.c.o
  -> SIGILL (same fixupsEnd trap), and the crashing atom's File::_path is now
     ".../libsyscall_32.dir/mach/clock_priv-i386-User.c.o"
A dylib-only filelist crashes differently (null deref inside
InputFiles::forEachInitialAtom), so the trap is specific to relocatable atoms,
but among them it is the first one processed, not a particular file.

## One line
The cctools ld64 dies (SIGILL/ud2) in Atom<x86_64>::fixupsEnd() on the first
relocatable atom processed; in the target's link that atom's input file is
src/external/xnu/darling/src/libsystem_kernel/libsyscall/CMakeFiles/libsyscall_32.dir/mach/clock-i386-User.c.o
(so the bisection's minimal crashing set is that single .o plus the dylib/.a inputs).
