/* font-families-probe: in a live process, [[NSDisplay currentDisplay] allFontFamilyNames] must answer a non-empty set (not raise
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

int main(void)
{
	long count = -1, faces = -1;
	const char *dc = "?", *imp = "?";
	printf("FAMILIES-START\n");
	fflush(stdout);
	id cls = objc_getClass("NSDisplay");
	id disp = cls ? MSG(id, cls, "currentDisplay") : 0;
	if (disp) {
		Dl_info di;
		void *m = class_getMethodImplementation(object_getClass(disp), sel_registerName("allFontFamilyNames"));
		if (m && dladdr(m, &di) && di.dli_fname) imp = di.dli_fname;
		dc = object_getClassName(disp);
		id set = MSG(id, disp, "allFontFamilyNames");
		if (set) {
			count = (long)MSG(unsigned long, set, "count");
			id any = MSG(id, set, "anyObject");
			if (any) {
				id arr = ((id (*)(id, void *, id))objc_msgSend)(disp, sel_registerName("fontTypefacesForFamilyName:"), any);
				faces = arr ? (long)MSG(unsigned long, arr, "count") : 0;
			}
		} else count = 0;
	}
	printf("FAMILIES-R count=%ld faces=%ld display=%s imp=%s\n", count, faces, dc, imp);
	printf("FAMILIES-DONE\n");
	fflush(stdout);
	return 0;
}
