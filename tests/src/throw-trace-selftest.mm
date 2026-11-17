/* throw-trace-selftest.mm - one ObjC @throw (caught) and one C++ throw (caught), no SDK headers needed. Run with the throw-trace hook
 * loaded; the runner counts the THROW-TRACE lines. Prints "SELFTEST-DONE". */
#include <stdio.h>
extern "C" {
extern void *objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern void *objc_msgSend(void *, void *, ...);
}
int main(void)
{
	void *o = objc_msgSend(objc_getClass("NSObject"), sel_registerName("new"));
	try { throw 5; } catch (int e) { printf("SELFTEST cxx caught %d\n", e); }
	@try { @throw (id)o; } @catch (id e) { printf("SELFTEST objc caught\n"); }
	printf("SELFTEST-DONE\n"); fflush(stdout);
	return 0;
}
