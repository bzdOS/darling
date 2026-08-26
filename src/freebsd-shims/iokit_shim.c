/*
 * iokit_shim.c -- minimal IOKit.framework replacement, just enough for
 * CoreGraphics.dylib to LINK and to behave honestly at runtime ("no such
 * device") when it calls the handful of IOKit entry points it actually
 * uses.
 *
 * WHY THIS EXISTS (see docs/SPEC-iokit-coregraphics-build.md for the full
 * writeup):
 *   - The real Apple IOKitUser is a *client* to XNU kernel IOKit (mach RPC,
 *     MIG-generated iokitmig.c, ~40 source files across hid/kext/pwr_mgt/
 *     usb/network/ps subprojects). None of that has anywhere to talk to on
 *     FreeBSD: darlingserver does not implement the IOKit mach registry,
 *     and this repo's src/external/iokitd submodule (the userspace daemon
 *     that WOULD answer those RPCs under Darling) is an uninitialized,
 *     empty submodule -- there is no `org.darlinghq.iokitd` to bootstrap
 *     look up, on any OS this port targets.
 *   - Grepping CoreGraphics/ (see the SPEC) shows exactly one file that
 *     calls into IOKit at all: CGDirectDisplay.m's CGDisplayIOServicePort(),
 *     used only to resolve a human-readable monitor name/EDID. bsdOS's
 *     entire display architecture is headless virtual displays
 *     (cage --headless, see CLAUDE.md "Stream pipeline") -- there is no
 *     physical monitor to name, so "no device found" is not a degraded
 *     answer here, it is the CORRECT answer.
 *   - So: implement exactly the symbols CGDirectDisplay.m's compiled object
 *     code references (verified by objdump-equivalent reading of the
 *     source, not by trusting any prior symbol count -- this file provides
 *     SIX symbols, not the four the first pass of the spec guessed, because
 *     IOIteratorNext() and IOObjectRelease() are also called unconditionally
 *     in CGDisplayIOServicePort()'s compiled body and therefore also need a
 *     resolvable symbol at CoreGraphics.dylib link time, even though this
 *     shim's own early-return behavior means they're never reached at
 *     runtime along the paths this shim exercises).
 *
 * KNOWN, DELIBERATE TECHNICAL DEBT: if src/external/iokitd is ever
 * implemented for real (a real HID/graphics device registry), this shim
 * must be replaced by the real IOKitUser + MIG path -- see SPEC §7. Do not
 * mistake this file for a permanent IOKit implementation.
 *
 * Never built or linked on a FreeBSD box -- see
 * build-freebsd/build-iokit-shim.sh's header comment and
 * docs/SPEC-iokit-coregraphics-build.md §7-8 for the full list of what is
 * and isn't verified.
 */

#include <CoreFoundation/CFString.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>

/*
 * purpose:     provide the well-known "use the default IOKit master port"
 *              sentinel that IOKitLib.h declares as `extern const mach_port_t
 *              kIOMasterPortDefault;` (IOKitLib.h:103).
 * input:       none (compile-time constant).
 * output:      MACH_PORT_NULL, exactly as the real IOKitLib.c does
 *              (IOKitUser/IOKitLib.c:112) -- this is not a shim-specific
 *              choice, it is what Apple's own client library ships; the
 *              actual master port is resolved lazily by IOMasterPort().
 * sideEffects: none.
 */
const mach_port_t kIOMasterPortDefault = MACH_PORT_NULL;

/*
 * purpose:     IOServiceMatching() (IOKitLib.h declares it at :1282,
 *              documented :1275-1279) -- build a matching dictionary
 *              selecting IOService objects by provider class name. Unlike
 *              every other symbol in this file, the real implementation
 *              (IOKitUser/IOKitLib.c:1051-1056, `MakeOneStringProp`) is a
 *              PURE CoreFoundation constructor with no mach/device
 *              dependency whatsoever -- it never touches a master port or
 *              an IOKit registry, it just builds
 *              {kIOProviderClassKey: CFString(name)}. This shim therefore
 *              reimplements it faithfully in full, not degraded, because
 *              there is nothing to degrade: it is still an undefined symbol
 *              CGDirectDisplay.m:404 needs resolved at CoreGraphics.dylib
 *              link time, and this shim does not link the real IOKitLib.c
 *              (see file header -- pulling that in would drag the ~40-file
 *              IOKitUser tree and its MIG-generated dependencies with it).
 * input:       name - a C string IOService provider class name (e.g.
 *                "IODisplayConnect", CGDirectDisplay.m:404).
 * output:      a new, owning-reference CFMutableDictionaryRef with one key
 *              (kIOProviderClassKey -> CFString(name)), or NULL if the
 *              dictionary/string/number could not be allocated -- matching
 *              the real function's own documented failure mode
 *              (IOKitLib.h:1279, "returned on success, or zero on
 *              failure").
 * sideEffects: allocates CF objects; caller (or, per convention,
 *              IOServiceGetMatchingServices() above) owns and must release
 *              the result.
 */
CFMutableDictionaryRef
IOServiceMatching(const char *name)
{
	CFMutableDictionaryRef dict;
	CFStringRef nameStr;

	if (name == NULL) {
		return NULL;
	}

	dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);
	if (dict == NULL) {
		return NULL;
	}

	nameStr = CFStringCreateWithCString(kCFAllocatorDefault, name,
			kCFStringEncodingUTF8);
	if (nameStr == NULL) {
		CFRelease(dict);
		return NULL;
	}

	CFDictionarySetValue(dict, CFSTR(kIOProviderClassKey), nameStr);
	CFRelease(nameStr);

	return dict;
}

/*
 * purpose:     honest "no such device" implementation of
 *              IOServiceGetMatchingServices() (IOKitLib.h:402-412). The real
 *              implementation serializes `matching` and sends a mach RPC
 *              (io_service_get_matching_services{_bin,_ool}) to the IOKit
 *              master port, which is answered by the kernel's IOKit device
 *              registry. No such registry exists anywhere in this port's
 *              stack (see file header) -- so this never attempts the RPC at
 *              all, it just reports the only truthful outcome: no matching
 *              service was found.
 * input:       masterPort - ignored (accepted for ABI compatibility with
 *                the real entry point; a real master port never exists to
 *                pass here anyway).
 *              matching   - a CF matching dictionary. Per the documented
 *                contract (IOKitLib.h:406) ownership of one reference is
 *                ALWAYS consumed by this call, success or failure -- this
 *                shim honors that and CFReleases it unconditionally, same
 *                as the real implementation does via IOCFSerialize + release
 *                (IOKitUser/IOKitLib.c:576).
 *              existing   - out-parameter for the resulting iterator.
 * output:      kIOReturnBadArgument if matching is NULL (matches the real
 *              function's own guard, IOKitUser/IOKitLib.c:560-561);
 *              otherwise kIOReturnNoDevice, with *existing set to
 *              MACH_PORT_NULL (== IO_OBJECT_NULL). Callers that check the
 *              iterator (as CGDisplayIOServicePort() does at
 *              CGDirectDisplay.m:408-410, `if (err) return 0;`) never reach
 *              the iterator in the error path.
 * sideEffects: releases `matching` (see input note above); does not touch
 *              mach, does not block, does not allocate.
 */
kern_return_t
IOServiceGetMatchingServices(
	mach_port_t masterPort,
	CFDictionaryRef matching,
	io_iterator_t *existing)
{
	(void)masterPort;

	if (existing != NULL) {
		*existing = MACH_PORT_NULL;
	}

	if (matching == NULL) {
		return kIOReturnBadArgument;
	}

	CFRelease(matching);
	return kIOReturnNoDevice;
}

/*
 * purpose:     provide a linkable, well-behaved IOIteratorNext()
 *              (IOKitLib.h:353-361) so CoreGraphics.dylib's compiled object
 *              code -- which calls it unconditionally inside
 *              CGDisplayIOServicePort()'s while-loop (CGDirectDisplay.m:412)
 *              -- has a symbol to bind against at link time.
 * input:       iterator - never a valid iterator in this port (this shim
 *                never hands one out; see IOServiceGetMatchingServices()
 *                above), but a caller could in principle pass any
 *                io_iterator_t / mach_port_t value.
 * output:      0 (IO_OBJECT_NULL), which per the documented contract
 *              ("or zero if no more remain or the iterator is invalid",
 *              IOKitLib.h:359-360) is exactly the correct answer for both
 *              of those cases -- every iterator handed out by this shim is
 *              invalid/empty by construction.
 * sideEffects: none.
 */
io_object_t
IOIteratorNext(io_iterator_t iterator)
{
	(void)iterator;
	return (io_object_t) MACH_PORT_NULL;
}

/*
 * purpose:     provide a linkable, well-behaved IOObjectRelease()
 *              (IOKitLib.h:228-236), called unconditionally after
 *              CGDisplayIOServicePort()'s while-loop (CGDirectDisplay.m:460).
 * input:       object - an io_object_t; always MACH_PORT_NULL in every path
 *                this shim can produce, but handled generically.
 * output:      kIOReturnSuccess unconditionally. Releasing a null/invalid
 *              object handle is a documented no-op-ish case for the real
 *              implementation too ("using the object after it has been
 *              released may or may not return an error", IOKitLib.h:232) --
 *              there is no real kernel object here to fail to release.
 * sideEffects: none.
 */
kern_return_t
IOObjectRelease(io_object_t object)
{
	(void)object;
	return kIOReturnSuccess;
}

/*
 * purpose:     honest "no information available" implementation of
 *              IODisplayCreateInfoDictionary() (graphics/IOGraphicsLib.h:
 *              60-70). The real implementation (IOKitUser/graphics.subproj/
 *              IODisplayLib.c:1377-1382, per the SPEC's reading) makes a
 *              further mach RPC against the `framebuffer` io_service_t --
 *              impossible here for the same reason as
 *              IOServiceGetMatchingServices() above, and in this shim's own
 *              runtime this function is never actually reachable from
 *              CGDisplayIOServicePort() (the calling loop body never
 *              executes, since IOServiceGetMatchingServices() always fails
 *              first) -- provided anyway so the symbol exists for the
 *              linker and behaves correctly if ever called directly.
 * input:       framebuffer - an io_service_t naming the framebuffer to
 *                describe; unused, since there is never a real one.
 *              options     - IODisplayDictionaryOptions bitmask; unused.
 * output:      NULL. This matches CFDictionaryCreate()'s own "on failure,
 *              returns NULL" convention, and callers of this specific API
 *              are documented to release the result "with CFRelease()"
 *              (IOGraphicsLib.h:65) -- CFRelease(NULL) is a safe no-op, so
 *              returning NULL here does not by itself violate that
 *              contract. NOTE (new finding, not in the SPEC's own reading --
 *              it explicitly stopped at CGDirectDisplay.m:421, see SPEC §8):
 *              CGDirectDisplay.m:420 immediately does
 *              CFDictionaryGetValue(info, ...) on this result without a
 *              NULL check first -- CFDictionaryGetValue(NULL, ...) is
 *              **not** documented as safe. Not worked around here (e.g. by
 *              returning an empty CFDictionaryCreate() instead of NULL)
 *              because CGDisplayIOServicePort() never actually reaches this
 *              call in this shim's own runtime -- the
 *              IOServiceGetMatchingServices() failure above returns first,
 *              so the while-loop this sits inside never executes. Flagged
 *              here, and in this task's report, as a latent risk for
 *              whoever next touches this path (e.g. if
 *              IOServiceGetMatchingServices() is ever made to "succeed"
 *              with an empty-but-valid iterator instead of erroring).
 * sideEffects: none.
 */
CFDictionaryRef
IODisplayCreateInfoDictionary(
	io_service_t framebuffer,
	IOOptionBits options)
{
	(void)framebuffer;
	(void)options;
	return NULL;
}
