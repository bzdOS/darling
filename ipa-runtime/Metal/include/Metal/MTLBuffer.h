/*
 * MTLBuffer.h — Metal buffer stub (GL buffer object backend)
 *
 * purpose:  Provide MTLBuffer lifecycle and CPU-side mapping.
 * input:    MTLBuffer obtained from MTLDeviceNewBuffer*.
 * output:   Mapped CPU pointer or GL buffer name for bind.
 * sideEffects: glBindBuffer / glBufferData / glMapBuffer.
 */

#pragma once

#include "MTLDevice.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * purpose:  Return byte length of buffer.
 */
size_t MTLBufferGetLength(MTLBuffer buf);

/*
 * purpose:  Return CPU-mapped pointer (persistent map via GL_MAP_WRITE_BIT).
 * output:   Writable pointer valid until MTLBufferRelease.
 * sideEffects: glMapBuffer on first call.
 */
void *MTLBufferGetContents(MTLBuffer buf);

/*
 * purpose:  Notify that a byte range was modified by CPU (calls glUnmapBuffer
 *           and re-uploads if needed).
 */
void MTLBufferDidModifyRange(MTLBuffer buf, size_t offset, size_t length);

/*
 * purpose:  Release GL buffer object and free wrapper struct.
 * sideEffects: glDeleteBuffers.
 */
void MTLBufferRelease(MTLBuffer buf);

/* Internal: return GL buffer name for binding by encoder. */
unsigned int MTLBufferGLName(MTLBuffer buf);

#ifdef __cplusplus
}
#endif
