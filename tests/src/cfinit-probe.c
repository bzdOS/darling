/* cfinit-probe: CoreFoundation must have run its initializer (__CFInitialize) by the time the image is loaded.
 * Real macOS CF does it from an LC_ROUTINES initializer. Prints CFINIT-PROBE and one CFINIT-VERDICT line:
 *   CFINIT-VERDICT pass|FAIL initialized=<__CFInitialized> string_typeid=<CFStringGetTypeID()> recall=<ok|bad|na>
 * pass = initialized==1 and string_typeid!=0 and a second explicit __CFInitialize call changes nothing. */
#include <dlfcn.h>
#include <stdio.h>

typedef unsigned long (*tid_fn)(void);
typedef void (*init_fn)(void);

int main(void)
{
	void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", RTLD_NOW);
	if (!cf) { printf("CFINIT-VERDICT FAIL initialized=-1 string_typeid=0 recall=na [dlopen: %s]\n", dlerror()); return 1; }
	volatile unsigned char *inited = (volatile unsigned char *)dlsym(cf, "__CFInitialized");
	tid_fn strtid = (tid_fn)dlsym(cf, "CFStringGetTypeID");
	init_fn initf = (init_fn)dlsym(cf, "__CFInitialize");
	if (!inited || !strtid) { printf("CFINIT-VERDICT FAIL initialized=-1 string_typeid=0 recall=na [dlsym %s]\n", !inited ? "__CFInitialized" : "CFStringGetTypeID"); return 1; }
	int i0 = *inited; unsigned long t0 = strtid();
	printf("CFINIT-PROBE initialized=%d string_typeid=%lu\n", i0, t0);
	const char *recall = "na";
	if (i0 && initf) { initf(); recall = (*inited == 1 && strtid() == t0) ? "ok" : "bad"; }
	int pass = i0 == 1 && t0 != 0 && (recall[0] == (char)111 || recall[0] == (char)110);
	printf("CFINIT-VERDICT %s initialized=%d string_typeid=%lu recall=%s\n", pass ? "pass" : "FAIL", i0, t0, recall);
	return pass ? 0 : 1;
}
