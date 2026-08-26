/*
 * IOLLEvent.h -- shim replacement.
 *
 * See src/freebsd-shims/missing-headers/README.md: `IOKit/hidsystem/
 * IOLLEvent.h` is a dangling symlink into the uninitialized `IOHIDFamily`
 * submodule everywhere else in this repo.
 * CoreGraphics/include/CoreGraphics/CoreGraphicsPrivate.h #includes this
 * path directly (line 4) and uses exactly one identifier from it,
 * `NXEventData`, purely as a struct member type (CoreGraphicsPrivate.h:96,
 * ":110" -- `CGSEventRecordData data; /o type-dependent data: 40 bytes o/`,
 * comment in the original). Verified: `grep -n 'NX' .../CoreGraphicsPrivate.h`
 * finds only that one identifier; no CoreGraphics or (checked) AppKit
 * source reads/writes specific NXEventData fields (only that struct member
 * declaration and its typedef exist anywhere in the grepped tree).
 *
 * RISK -- NOT VERIFIED: the real NXEventData is a union of several
 * event-specific structs (key/mouse/tablet data) whose exact layout is not
 * reconstructed here -- only the documented overall size (40 bytes, per
 * the comment cited above) is honored, as an opaque byte blob. If any code
 * anywhere in this tree (outside the CoreGraphics/AppKit files actually
 * grepped for this task) reads/writes specific NXEventData fields, this
 * stub will not match and needs real field definitions.
 */
#ifndef BSDOS_SHIM_IOLLEVENT_H
#define BSDOS_SHIM_IOLLEVENT_H

typedef union {
	unsigned char opaque[40];
} NXEventData;

#endif /* BSDOS_SHIM_IOLLEVENT_H */
