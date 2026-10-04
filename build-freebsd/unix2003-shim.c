/*
 * unix2003-shim.c — guest Mach-O dylib providing the legacy UNIX2003 aliases
 * that the rebuilt libsystem_malloc.dylib imports.
 *
 * Why: after lane 105-3 the run reaches a new dyld failure
 *   Symbol not found: _mprotect$UNIX2003
 *   Referenced from: /usr/lib/system/libsystem_malloc.dylib
 *   Expected in: flat namespace
 * The rebuilt libsystem_malloc imports _kill$UNIX2003, _mprotect$UNIX2003,
 * _sleep$UNIX2003, _write$UNIX2003; the guest system dylibs export only the
 * unsuffixed names.  The message says "flat namespace", so the lookup scans
 * all loaded images: the aliases can be supplied from the side, without
 * touching libsystem_malloc.
 *
 * Each function is a pure thunk to the unsuffixed base (same ABI; the
 * UNIX2003 suffix is only a name decoration).  The `$` cannot appear in a C
 * identifier, so each thunk is given the real linker name with an asm label.
 *
 * Built as a guest dylib with the same raw clang + ld64.lld recipe as
 * build-iokit-shim.sh, linked against the staged overlay's
 * libsystem_kernel.dylib / libsystem_c.dylib / libSystem.B.dylib.
 */

extern int kill(int pid, int sig);
extern int mprotect(void *addr, unsigned long len, int prot);
extern long write(int fd, const void *buf, unsigned long n);
extern unsigned int sleep(unsigned int seconds);

int unix2003_kill(int pid, int sig) __asm__("_kill$UNIX2003");
int unix2003_kill(int pid, int sig) { return kill(pid, sig); }

int unix2003_mprotect(void *addr, unsigned long len, int prot) __asm__("_mprotect$UNIX2003");
int unix2003_mprotect(void *addr, unsigned long len, int prot) { return mprotect(addr, len, prot); }

long unix2003_write(int fd, const void *buf, unsigned long n) __asm__("_write$UNIX2003");
long unix2003_write(int fd, const void *buf, unsigned long n) { return write(fd, buf, n); }

unsigned int unix2003_sleep(unsigned int seconds) __asm__("_sleep$UNIX2003");
unsigned int unix2003_sleep(unsigned int seconds) { return sleep(seconds); }
