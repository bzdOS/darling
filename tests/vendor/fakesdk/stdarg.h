/* Minimal freestanding stdarg.h.
 *
 * This FreeBSD-packaged clang splits stdarg.h into modular __stdarg_*.h
 * fragments meant to be assembled by the TARGET libc's own umbrella header
 * (the pattern glibc/musl use) — but there's no such umbrella here, and
 * Apple's own macOS SDK doesn't ship one either (a real Xcode install
 * provides it alongside its own clang, not via the SDK). Bypass the
 * fragments entirely with the classic, portable compiler-builtin form.
 */
#ifndef _STDARG_H
#define _STDARG_H
typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#endif
