/* Minimal AE.h stub (AppleEvent Manager / Carbon).
 *
 * Like CoreGraphics/CFNetwork, the AE framework has no vendored
 * implementation in this tree at all. Foundation/NSAppleEventDescriptor.h
 * used to define these typedefs itself as a fallback, but that block is
 * now commented out ("We have proper headers for AppleEvents now") in
 * favor of getting them from here — these are exactly those same,
 * unchanged-for-years, type-only Apple public definitions (plain ints and
 * an opaque struct, no function bodies or linkable symbols). Nothing
 * built here actually calls a real AppleEvent Manager function. */
#ifndef AE_AE_H_
#define AE_AE_H_

typedef int AEEventClass;
typedef int AEEventID;
typedef int AEReturnID;
typedef unsigned int AEKeyword;
typedef int AETransactionID;
typedef int DescType;

struct AEDesc;
typedef struct AEDesc AppleEvent;

#endif
