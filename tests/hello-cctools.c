/*
 * hello-cctools.c — trivial real macOS C program, source for
 * hello-cctools-macho.
 *
 * purpose:     Prove the loader can run a Mach-O binary built by a REAL
 *              toolchain (clang + LLVM's ld64.lld linking against the
 *              actual libSystem.B.dylib), not one hand-assembled byte by
 *              byte like gen-hello-dynamic-macho.py's output. puts() here
 *              goes through the same symbol-binding path as any real
 *              program's libc calls.
 * input:       None.
 * output:      "hello-cctools\n" on stdout.
 * sideEffects: None.
 *
 * Build: see build-freebsd/build-real-macho-tests.sh.
 */
extern int puts(const char *);

int main(void) {
    puts("hello-cctools");
    return 0;
}
