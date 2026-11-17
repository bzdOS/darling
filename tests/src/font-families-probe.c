/* font-families-probe: [[NSDisplay currentDisplay] allFontFamilyNames] must answer a non-empty set (not raise "only defined for abstract class"),
 * and -fontTypefacesForFamilyName: (called right after it by +[NSFontFamily buildFontFamilies]) must answer a non-empty array.
 * An uncaught objc exception kills the process before the result line; the script reads that as threw=y.
 * Output: "FAMILIES-R count=<n> faces=<n> display=<class>" then "FAMILIES-DONE". */
#include <stdio.h>

typedef void *id;
extern id objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern id objc_msgSend(id, void *, ...);
extern const char *object_getClassName(id);

#define MSG(r, o, s) ((r (*)(id, void *))objc_msgSend)((o), sel_registerName(s))

int main(void)
{
	long count = -1, faces = -1;
	const char *dc = "?";
	printf("FAMILIES-START\n");
	fflush(stdout);
	id cls = objc_getClass("NSDisplay");
	id disp = cls ? MSG(id, cls, "currentDisplay") : 0;
	if (disp) {
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
	printf("FAMILIES-R count=%ld faces=%ld display=%s\n", count, faces, dc);
	printf("FAMILIES-DONE\n");
	fflush(stdout);
	return 0;
}
