/* throw-trace-hook: logs every ObjC exception (class, reason; for an unrecognized selector the reason carries class and selector)
 * and every C++ throw (type name) with the caller's return address, then lets the throw proceed. Loaded with
 * DYLD_INSERT_LIBRARIES; it only does anything when DARLING_THROW_TRACE is set in the environment. Lines on stderr:
 *   THROW-TRACE objc class=<C> name=<N> reason=<R> ret=0x<addr>
 *   THROW-TRACE cxx type=<mangled> ret=0x<addr>
 * Built by throw-trace-hook.sh. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

extern void objc_exception_throw(void *);
extern void __cxa_throw(void *, void *, void (*)(void *));
extern void *objc_msgSend(void *, void *, ...);
extern void *sel_registerName(const char *);
extern void *object_getClass(void *);
extern const char *class_getName(void *);
extern int class_respondsToSelector(void *, void *);

static int on(void)
{
	static int v = -1;
	if (v < 0) v = getenv("DARLING_THROW_TRACE") != 0;
	return v;
}
static const char *str_of(void *o)
{
	if (!o) return "?";
	if (!class_respondsToSelector(object_getClass(o), sel_registerName("UTF8String"))) return "?";
	const char *s = (const char *)objc_msgSend(o, sel_registerName("UTF8String"));
	return s ? s : "?";
}
static void my_objc_throw(void *exc)
{
	if (on()) {
		char buf[1200];
		const char *cn = exc ? class_getName(object_getClass(exc)) : "nil", *nm = "?", *rs = "?";
		if (exc && class_respondsToSelector(object_getClass(exc), sel_registerName("reason"))) {
			nm = str_of(objc_msgSend(exc, sel_registerName("name")));
			rs = str_of(objc_msgSend(exc, sel_registerName("reason")));
		}
		int n = snprintf(buf, sizeof buf, "THROW-TRACE objc class=%s name=%.100s reason=%.800s ret=%p\n", cn, nm, rs,
		    __builtin_return_address(0));
		if (n > 0) write(2, buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
	}
	objc_exception_throw(exc);
	__builtin_unreachable();
}
static void my_cxa_throw(void *obj, void *tinfo, void (*dtor)(void *))
{
	if (on()) {
		char buf[400];
		const char *tn = tinfo ? *(const char **)((char *)tinfo + 8) : "?";
		int n = snprintf(buf, sizeof buf, "THROW-TRACE cxx type=%.200s ret=%p\n", tn ? tn : "?", __builtin_return_address(0));
		if (n > 0) write(2, buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
	}
	__cxa_throw(obj, tinfo, dtor);
	__builtin_unreachable();
}
struct interpose { void *replacement, *replacee; };
__attribute__((used, section("__DATA,__interpose"))) static struct interpose interposers[] = {
	{ (void *)my_objc_throw, (void *)objc_exception_throw },
	{ (void *)my_cxa_throw, (void *)__cxa_throw },
};
