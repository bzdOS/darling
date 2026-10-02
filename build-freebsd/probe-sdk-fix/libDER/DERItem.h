/*
 * Copyright (c) 2020 Apple Inc. All Rights Reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

/*
 * Fixed include order — installed over the sdk-flat copy by
 * build-freebsd/build-guest-wl-probe.sh.
 *
 * What was broken: the original header claimed `_DER_ITEM_H_` at the top
 * and then included <libDER/libDER_config.h>, which itself includes
 * "libDER/oids.h". oids.h checks `#if !defined(_SECURITY_OIDS_H_) &&
 * !defined(_DER_ITEM_H_)` before its own DERItem typedef — with the
 * guard already claimed by this file's first lines, the block is
 * skipped, and oids.h then USES DERItem in its `extern const DERItem
 * oidRsa...` declarations: "unknown type name 'DERItem'" on every
 * translation unit that reaches libDER through DERItem.h first (hit
 * live building the session probe via Foundation -> Security ->
 * oids.h; the same cycle fires in the DARLING-defer path because the
 * top guard is claimed even when the typedef is skipped).
 *
 * The fix: provide DERByte/DERSize and the DERItem typedef BEFORE the
 * config include, claim `_DER_ITEM_H_` only on the non-deferring path,
 * and leave the deferring (DARLING) path guard-free so oids.h's own
 * block still runs when this header came first. Identical typedef
 * redefinitions are legal C11, so oids.h's duplicate DERByte/DERSize
 * in the deferring path is fine. Every include order is covered:
 * DERItem.h-first (either branch), oids.h-first, config-first.
 */
#include <stdint.h>
#include <stddef.h>

#ifndef _LIB_DER_CONFIG_H_
/* base types DERItem needs, without pulling oids.h through the config */
typedef uint8_t DERByte;
typedef size_t DERSize;
#ifndef DER_counted_by
#define DER_counted_by(x)
#endif
/*
 * Primary representation of a block of memory.
 */
#ifndef DARLING
// DERItem is already defined in libDER/oids.h on the deferring path
typedef struct {
    DERByte        *DER_counted_by(length) data;
    DERSize        length;
} DERItem;
#define _DER_ITEM_H_
#endif
#endif    /* _LIB_DER_CONFIG_H_ */

#include <libDER/libDER_config.h>
