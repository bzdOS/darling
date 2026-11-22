#!/usr/bin/env python3
"""traplog-convert.py <root> - mechanical conversion of fprintf(stderr, ...) trap-log lines to TRAPLOG(...)
(non-blocking write(2) from a stack buffer, see trap_log.h) inside the handler-path functions of mldr.
<root> is a source root holding src/startup/mldr/. Only the functions named in TARGETS are touched; every
other fprintf(stderr, ...) (loader, startup, setup, exit-time reports) stays and is listed with --list.
fflush(stderr); inside converted functions is dropped (write(2) is unbuffered). Idempotent."""
import re, sys, os

TARGETS = {
    "freebsd_syscall_trap.c": {
        "dispatch_macos_bsd_syscall", "dispatch_mach_trap", "mldr_report_trap", "mldr_note_mapping",
        "dispatch_linux_syscall_host_errno", "sigsys_handler_inner", "sigill_handler_inner",
        "sigtrap_handler", "crash_debug_handler"},
    "sigaction_translate.c": {"sat_guest_handler_trampoline"},
    "crash_dump.c": {"mldr_dump_guarded_stack"},
}
FUNC = re.compile(r'^(?!#|\}|\s)[^;=]*?\b(\w+)\s*\(')

def convert(path, funcs, listing):
    lines = open(path).read().split('\n')
    out, cur, nconv, nfl, left = [], None, 0, 0, []
    for i, l in enumerate(lines):
        if l.startswith('}'):
            cur = None
        else:
            m = FUNC.match(l)
            if m and not l.rstrip().endswith(';'):
                cur = m.group(1)
        if 'fprintf(stderr,' in l:
            if cur in funcs:
                l = l.replace('fprintf(stderr,', 'TRAPLOG(')
                l = l.replace('TRAPLOG( ', 'TRAPLOG(')
                nconv += 1
            else:
                left.append((i + 1, cur, l.strip()[:90]))
        elif cur in funcs and l.strip() == 'fflush(stderr);' or (cur in funcs and re.match(r'^\s*fflush\(stderr\);\s*/\*.*\*/\s*$', l)):
            prev = next((x for x in reversed(out) if x.strip()), '').rstrip()
            if prev.endswith((';', '{', '}', '*/', ':')):
                nfl += 1
                continue
            l = re.sub(r'fflush\(stderr\);', '(void)0;', l)
        out.append(l)
    if not listing:
        open(path, 'w').write('\n'.join(out))
    return nconv, nfl, left

def add_include(path, after_pat):
    s = open(path).read()
    if '"trap_log.h"' in s:
        return
    m = re.search(after_pat, s, re.M)
    s = s[:m.end()] + '\n#include "trap_log.h"' + s[m.end():]
    open(path, 'w').write(s)

def patch_header(path):
    s = open(path).read()
    if 'TRAPLOG(' in s:
        return
    add = '''
/* TRAPLOG(fmt, ...): handler-safe replacement for fprintf(stderr, ...). Formats with vsnprintf into a stack buffer and
 * emits it with one write(2): no stdio FILE lock, no malloc, no buffering. Use it (not fprintf) in signal, trap and
 * suspend/resume paths; a thread suspended while holding the stderr FILE lock would block every other logging thread. */
static inline __attribute__((format(printf, 1, 2))) void
mldr_tl_printf(const char *fmt, ...)
{
	char b[1024];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (n <= 0)
		return;
	if (n >= (int)sizeof(b))
		n = (int)sizeof(b) - 1;
	(void)!write(2, b, (size_t)n);
}
#define TRAPLOG(...) mldr_tl_printf(__VA_ARGS__)
'''
    s = s.replace('#include <sys/thr.h> /* thr_self */', '#include <sys/thr.h> /* thr_self */\n#include <stdarg.h>\n#include <stdio.h>   /* vsnprintf only */', 1)
    i = s.rindex('#endif')
    s = s[:i] + add.lstrip('\n') + '\n' + s[i:]
    open(path, 'w').write(s)

BT_OLD = """        char **names = backtrace_symbols(frames, n);
        TRAPLOG("  backtrace (%d frames):\\n", n);
        for (int i = 0; i < n; i++) {
            TRAPLOG("    #%02d %p  %s\\n", i, frames[i],
                    (names && names[i]) ? names[i] : "?");
        }
        if (names) free(names);
"""
BT_NEW = """        TRAPLOG("  backtrace (%d frames):\\n", n);
        /* backtrace_symbols_fd, not backtrace_symbols: no malloc in the crash handler */
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
"""

def backtrace_fd(path):
    s = open(path).read()
    if BT_OLD in s:
        open(path, 'w').write(s.replace(BT_OLD, BT_NEW))

def main():
    listing = '--list' in sys.argv
    root = [a for a in sys.argv[1:] if not a.startswith('--')][0]
    d = os.path.join(root, 'src/startup/mldr')
    tot = 0
    for f, funcs in TARGETS.items():
        n, nf, left = convert(os.path.join(d, f), funcs, listing)
        tot += n
        print('%s: converted %d, dropped %d fflush, left %d' % (f, n, nf, len(left)))
        if listing:
            for ln, fn, t in left: print('  left %d [%s] %s' % (ln, fn, t))
    if not listing:
        backtrace_fd(os.path.join(d, 'freebsd_syscall_trap.c'))
        patch_header(os.path.join(d, 'trap_log.h'))
        add_include(os.path.join(d, 'sigaction_translate.c'), r'^#include "linux_ucontext.h"')
        add_include(os.path.join(d, 'crash_dump.c'), r'^#include "crash_dump.h"')
    print('total converted', tot)
main()
