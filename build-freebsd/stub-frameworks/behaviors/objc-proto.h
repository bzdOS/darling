/* objc-proto.h - the ObjC runtime prototypes every behaviour source shares (no SDK headers in the stub build).
 * Each behaviour declares the further runtime functions it uses itself. */
#ifndef BEHAVIORS_OBJC_PROTO_H
#define BEHAVIORS_OBJC_PROTO_H
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *id;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern id objc_msgSend(id, SEL, ...);
/* MSG(ret, arg types...) casts objc_msgSend to the exact signature of the call */
#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
#define S(x) sel_registerName(x)
#endif
