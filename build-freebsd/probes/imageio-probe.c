/* imageio-probe: outside Chrome, the real ImageIO decodes a generated 8-bit RGBA PNG (4x3) through CGImageSourceCreateWithData /
 * CGImageSourceCreateImageAtIndex with matching width and height; garbage bytes give a NULL source; and
 * +[NSBitmapImageRep canInitWithData:] on garbage answers NO and does not crash (AppKit used to CFRelease the NULL source).
 * Output: "IMAGEIO-R w=<n> h=<n> count=<n> garbage=<null|obj>", "IMGREP-BEGIN", "IMGREP-R canInit=<0|1>", "IMAGEIO-PROBE-DONE". */
#include <stdio.h>
#include <string.h>
typedef void *id;
typedef const void *CFTypeRef;
extern id objc_getClass(const char *);
extern void *sel_registerName(const char *);
extern id objc_msgSend(id, void *, ...);
extern CFTypeRef CFDataCreate(void *, const unsigned char *, long);
extern void CFRelease(CFTypeRef);
extern CFTypeRef CGImageSourceCreateWithData(CFTypeRef, CFTypeRef);
extern CFTypeRef CGImageSourceCreateImageAtIndex(CFTypeRef, unsigned long, CFTypeRef);
extern unsigned long CGImageSourceGetCount(CFTypeRef);
extern unsigned long CGImageGetWidth(CFTypeRef);
extern unsigned long CGImageGetHeight(CFTypeRef);
extern void NSApplicationLoad(void);
static const unsigned char png[] = { 0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x03,0x08,0x06,0x00,0x00,0x00,0xb4,0xf4,0xae,0xc6,0x00,0x00,0x00,0x2a,0x49,0x44,0x41,0x54,0x78,0x9c,0x15,0xc8,0x31,0x01,0x00,0x20,0x10,0xc4,0xb0,0x0a,0x43,0xc4,0xcb,0xb9,0x11,0x29,0x88,0x38,0x7f,0x50,0x86,0x2c,0x01,0xf6,0x5d,0x8a,0x2a,0x18,0x43,0x51,0xe7,0xc7,0x31,0x14,0x55,0x0f,0x50,0x09,0x19,0xed,0x8a,0xb8,0xb6,0x3d,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82 };
int main(void)
{
	unsigned long w = 0, h = 0, n = 0;
	CFTypeRef d = CFDataCreate(0, png, sizeof png);
	CFTypeRef s = CGImageSourceCreateWithData(d, 0);
	if (s) {
		n = CGImageSourceGetCount(s);
		CFTypeRef im = CGImageSourceCreateImageAtIndex(s, 0, 0);
		if (im) { w = CGImageGetWidth(im); h = CGImageGetHeight(im); }
	}
	unsigned char junk[64];
	memset(junk, 0x5a, sizeof junk);
	CFTypeRef jd = CFDataCreate(0, junk, sizeof junk);
	CFTypeRef js = CGImageSourceCreateWithData(jd, 0);
	printf("IMAGEIO-R w=%lu h=%lu count=%lu garbage=%s\n", w, h, n, js ? "obj" : "null");
	printf("IMGREP-BEGIN\n");
	fflush(stdout);
	id cls = objc_getClass("NSBitmapImageRep");
	long ok = cls ? (long)((unsigned char (*)(id, void *, id))objc_msgSend)(cls, sel_registerName("canInitWithData:"), (id)jd) : -1;
	printf("IMGREP-R canInit=%ld\n", ok);
	printf("IMAGEIO-PROBE-DONE\n");
	fflush(stdout);
	return 0;
}
