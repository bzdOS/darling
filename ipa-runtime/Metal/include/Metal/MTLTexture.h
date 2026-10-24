/*
 * MTLTexture.h — Metal texture stub (GL texture 2D backend)
 *
 * purpose:  Provide MTLTexture creation, upload, and framebuffer attachment.
 * input:    Dimensions + pixel format.
 * output:   Opaque MTLTexture handle wrapping a GL texture name.
 * sideEffects: glGenTextures / glTexImage2D / glBindTexture.
 */

#pragma once

#include "MTLDevice.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * purpose:  Upload pixel data to texture.
 * input:    bytes — pixel data in the texture's pixel format;
 *           bytesPerRow — stride in bytes.
 * sideEffects: glTexSubImage2D.
 */
void MTLTextureReplaceRegion(MTLTexture tex,
                              uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height,
                              const void *bytes,
                              size_t bytesPerRow);

/*
 * purpose:  Read back pixel data from texture (slow path — glReadPixels).
 * output:   Writes into caller-supplied buffer.
 */
void MTLTextureGetBytes(MTLTexture tex,
                         void *pixelBytes,
                         size_t bytesPerRow,
                         uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height);

uint32_t MTLTextureGetWidth(MTLTexture tex);
uint32_t MTLTextureGetHeight(MTLTexture tex);
MTLPixelFormat MTLTextureGetPixelFormat(MTLTexture tex);

/*
 * purpose:  Release GL texture object and free wrapper.
 * sideEffects: glDeleteTextures.
 */
void MTLTextureRelease(MTLTexture tex);

/* Internal: return GL texture name for binding by encoder/framebuffer. */
unsigned int MTLTextureGLName(MTLTexture tex);

#ifdef __cplusplus
}
#endif
