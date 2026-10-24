/*
 * MTLCommandQueue.h — Metal command queue + command buffer + render encoder stubs
 *
 * purpose:  Thin wrappers that translate Metal recording API to immediate-mode
 *           OpenGL ES 2.0 draw calls.
 * input:    MTLCommandQueue obtained from MTLDeviceNewCommandQueue.
 * output:   Committed draws appear in the bound GL framebuffer.
 * sideEffects: GL state mutations during encoding/commit.
 */

#pragma once

#include "MTLDevice.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MTLCommandBuffer_t         *MTLCommandBuffer;
typedef struct MTLRenderCommandEncoder_t  *MTLRenderCommandEncoder;
typedef struct MTLBlitCommandEncoder_t    *MTLBlitCommandEncoder;

/* Primitive topology */
typedef enum {
    MTLPrimitiveTypePoint         = 0,
    MTLPrimitiveTypeLine          = 1,
    MTLPrimitiveTypeLineStrip     = 2,
    MTLPrimitiveTypeTriangle      = 3,
    MTLPrimitiveTypeTriangleStrip = 4,
} MTLPrimitiveType;

/* Index type */
typedef enum {
    MTLIndexTypeUInt16 = 0,
    MTLIndexTypeUInt32 = 1,
} MTLIndexType;

/* Load/store actions (no-op in GL stub) */
typedef enum {
    MTLLoadActionDontCare = 0,
    MTLLoadActionLoad     = 1,
    MTLLoadActionClear    = 2,
} MTLLoadAction;

typedef enum {
    MTLStoreActionDontCare = 0,
    MTLStoreActionStore    = 1,
} MTLStoreAction;

/* Clear colour */
typedef struct { double red, green, blue, alpha; } MTLClearColor;

static inline MTLClearColor MTLClearColorMake(double r, double g,
                                               double b, double a) {
    MTLClearColor c = {r, g, b, a};
    return c;
}

/* Render pass attachment descriptor */
typedef struct {
    MTLTexture     texture;       /* NULL → default framebuffer */
    MTLLoadAction  loadAction;
    MTLStoreAction storeAction;
    MTLClearColor  clearColor;
} MTLRenderPassColorAttachmentDescriptor;

typedef struct {
    MTLRenderPassColorAttachmentDescriptor colorAttachments[8];
    uint32_t colorAttachmentCount;
} MTLRenderPassDescriptor;

/* Viewport */
typedef struct {
    double originX, originY, width, height, znear, zfar;
} MTLViewport;

/* --- Command queue --- */

/*
 * purpose:  Release a command queue.
 * sideEffects: none (GL context not destroyed).
 */
void MTLCommandQueueRelease(MTLCommandQueue queue);

/*
 * purpose:  Allocate a command buffer for recording.
 * output:   New MTLCommandBuffer or NULL.
 */
MTLCommandBuffer MTLCommandQueueCommandBuffer(MTLCommandQueue queue);

/* --- Command buffer --- */

/*
 * purpose:  Begin a render pass and return an encoder.
 * input:    descriptor — attachment + clear config.
 * output:   New MTLRenderCommandEncoder or NULL.
 * sideEffects: Binds framebuffer / calls glClear if loadAction==Clear.
 */
MTLRenderCommandEncoder MTLCommandBufferRenderCommandEncoderWithDescriptor(
    MTLCommandBuffer buf,
    const MTLRenderPassDescriptor *descriptor);

/*
 * purpose:  Submit the recorded commands (no-op in immediate GL mode).
 * sideEffects: Calls glFlush.
 */
void MTLCommandBufferCommit(MTLCommandBuffer buf);

/*
 * purpose:  Wait until GPU work is done (calls glFinish).
 */
void MTLCommandBufferWaitUntilCompleted(MTLCommandBuffer buf);

void MTLCommandBufferRelease(MTLCommandBuffer buf);

/* --- Render command encoder --- */

void MTLRenderCommandEncoderSetViewport(MTLRenderCommandEncoder enc,
                                         MTLViewport viewport);

void MTLRenderCommandEncoderSetRenderPipelineState(
    MTLRenderCommandEncoder enc, MTLRenderPipelineState pso);

/*
 * purpose:  Bind an MTLBuffer to a vertex attribute slot.
 * input:    index — attribute index; offset — byte offset into buffer.
 */
void MTLRenderCommandEncoderSetVertexBuffer(MTLRenderCommandEncoder enc,
                                             MTLBuffer buf,
                                             uint32_t offset,
                                             uint32_t index);

void MTLRenderCommandEncoderSetFragmentTexture(MTLRenderCommandEncoder enc,
                                                MTLTexture tex,
                                                uint32_t index);

/*
 * purpose:  Issue a non-indexed draw call.
 */
void MTLRenderCommandEncoderDrawPrimitives(MTLRenderCommandEncoder enc,
                                            MTLPrimitiveType type,
                                            uint32_t vertexStart,
                                            uint32_t vertexCount);

/*
 * purpose:  Issue an indexed draw call.
 */
void MTLRenderCommandEncoderDrawIndexedPrimitives(
    MTLRenderCommandEncoder enc,
    MTLPrimitiveType type,
    uint32_t indexCount,
    MTLIndexType indexType,
    MTLBuffer indexBuffer,
    uint32_t indexBufferOffset);

/*
 * purpose:  End encoding (no-op in immediate GL mode).
 */
void MTLRenderCommandEncoderEndEncoding(MTLRenderCommandEncoder enc);

void MTLRenderCommandEncoderRelease(MTLRenderCommandEncoder enc);

#ifdef __cplusplus
}
#endif
