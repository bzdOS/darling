/* Minimal freestanding stdbool.h — see stdarg.h in this same directory for
 * why: this FreeBSD clang install ships no full stdbool.h and neither does
 * the vendored macOS SDK (both expect it from the other side). */
#ifndef _STDBOOL_H
#define _STDBOOL_H
#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif
