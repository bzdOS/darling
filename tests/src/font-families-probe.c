/* font-families-probe: in a live process, -[WaylandDisplay allFontFamilyNames] must answer a non-empty set (not raise
 * "only defined for abstract class"), -fontTypefacesForFamilyName: must answer a non-empty array, and the IMP serving
 * -allFontFamilyNames must lie in the AppKitExtras image (dladdr). An uncaught objc exception kills the process before the result
 * line; the script reads that as threw=y.
 * Output: "FAMILIES-R count=<n> faces=<n> display=<class> imp=<image path>" then "FAMILIES-DONE". */
#include <stdio.h>

typedef void *id;
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
extern int dladdr(const void *, Dl_info *);
extern id objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern id objc_msgSend(id, void *, ...);
extern const char *object_getClassName(id);
extern id object_getClass(id);
extern void *class_getMethodImplementation(id, void *);

#define MSG(r, o, s) ((r (*)(id, void *))objc_msgSend)((o), sel_registerName(s))

extern void *dlopen(const char *, int);
extern char *dlerror(void);

/* The compositor is not needed: [NSDisplay currentDisplay] would raise "Failed to connect to a window server" here. The backend bundle is
 * loaded directly and the IMPs of the live class WaylandDisplay are called (the replacements ignore the receiver). */
int main(void)
{
	long count = -1, faces = -1;
	const char *dc = "?", *imp = "?";
	Dl_info di;
	printf("FAMILIES-START\n");
	fflush(stdout);
	void *h = dlopen("/System/Library/Frameworks/AppKit.framework/Resources/Backends/Wayland.backend/Contents/MacOS/Wayland", 2);
	if (!h) printf("FAMILIES-DLOPEN %s\n", dlerror());
	id cls = objc_getClass("WaylandDisplay");
	if (cls) {
		dc = "WaylandDisplay";
		void *m = class_getMethodImplementation(cls, sel_registerName("allFontFamilyNames"));
		void *t = class_getMethodImplementation(cls, sel_registerName("fontTypefacesForFamilyName:"));
		if (m && dladdr(m, &di) && di.dli_fname) imp = di.dli_fname;
		id set = ((id (*)(id, void *))m)(cls, sel_registerName("allFontFamilyNames"));
		if (set) {
			count = (long)MSG(unsigned long, set, "count");
			id any = MSG(id, set, "anyObject");
			if (any) {
				id arr = ((id (*)(id, void *, id))t)(cls, sel_registerName("fontTypefacesForFamilyName:"), any);
				faces = arr ? (long)MSG(unsigned long, arr, "count") : 0;
			}
		} else count = 0;
	}
	printf("FAMILIES-R count=%ld faces=%ld display=%s imp=%s\n", count, faces, dc, imp);
	printf("FAMILIES-DONE\n");
	fflush(stdout);
	return 0;
}
