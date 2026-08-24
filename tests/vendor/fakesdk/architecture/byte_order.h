/* Minimal architecture/byte_order.h — Apple's old "basic-headers" package
 * ships this directory but its checkout in this repo has an empty
 * architecture/ subdirectory (no submodule declares it, so there's nothing
 * to `git submodule update --init` for it — a genuine content gap, not a
 * missing-init one). This is the standard, portable byte-swap API real
 * byte_order.h implementations provide, expressed with compiler builtins so
 * it needs no per-architecture assembly. */
#ifndef _ARCHITECTURE_BYTE_ORDER_H_
#define _ARCHITECTURE_BYTE_ORDER_H_

#include <stdint.h>

static inline unsigned short NXSwapShort(unsigned short x) {
    return __builtin_bswap16(x);
}
static inline unsigned int NXSwapInt(unsigned int x) {
    return __builtin_bswap32(x);
}
static inline unsigned long NXSwapLong(unsigned long x) {
    return sizeof(long) == 8 ? __builtin_bswap64((uint64_t)x) : __builtin_bswap32((uint32_t)x);
}
static inline unsigned long long NXSwapLongLong(unsigned long long x) {
    return __builtin_bswap64(x);
}

#if __LITTLE_ENDIAN__
#define NXSwapHostShortToBig(x) NXSwapShort(x)
#define NXSwapBigShortToHost(x) NXSwapShort(x)
#define NXSwapHostIntToBig(x) NXSwapInt(x)
#define NXSwapBigIntToHost(x) NXSwapInt(x)
#define NXSwapHostLongToBig(x) NXSwapLong(x)
#define NXSwapBigLongToHost(x) NXSwapLong(x)
#define NXSwapHostLongLongToBig(x) NXSwapLongLong(x)
#define NXSwapBigLongLongToHost(x) NXSwapLongLong(x)
#define NXSwapHostShortToLittle(x) (x)
#define NXSwapLittleShortToHost(x) (x)
#define NXSwapHostIntToLittle(x) (x)
#define NXSwapLittleIntToHost(x) (x)
#define NXSwapHostLongToLittle(x) (x)
#define NXSwapLittleLongToHost(x) (x)
#define NXSwapHostLongLongToLittle(x) (x)
#define NXSwapLittleLongLongToHost(x) (x)
#else
#define NXSwapHostShortToBig(x) (x)
#define NXSwapBigShortToHost(x) (x)
#define NXSwapHostIntToBig(x) (x)
#define NXSwapBigIntToHost(x) (x)
#define NXSwapHostLongToBig(x) (x)
#define NXSwapBigLongToHost(x) (x)
#define NXSwapHostLongLongToBig(x) (x)
#define NXSwapBigLongLongToHost(x) (x)
#define NXSwapHostShortToLittle(x) NXSwapShort(x)
#define NXSwapLittleShortToHost(x) NXSwapShort(x)
#define NXSwapHostIntToLittle(x) NXSwapInt(x)
#define NXSwapLittleIntToHost(x) NXSwapInt(x)
#define NXSwapHostLongToLittle(x) NXSwapLong(x)
#define NXSwapLittleLongToHost(x) NXSwapLong(x)
#define NXSwapHostLongLongToLittle(x) NXSwapLongLong(x)
#define NXSwapLittleLongLongToHost(x) NXSwapLongLong(x)
#endif

#endif /* _ARCHITECTURE_BYTE_ORDER_H_ */
