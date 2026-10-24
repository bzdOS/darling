/*
 * MTLRenderPipeline.h — Metal render pipeline state stub (GL program backend)
 *
 * purpose:  Bundle vertex + fragment MTLFunction into a GL program object.
 * input:    MTLRenderPipelineDescriptor with vertex/fragment functions.
 * output:   MTLRenderPipelineState wrapping a compiled/linked GL program.
 * sideEffects: glCreateProgram / glAttachShader / glLinkProgram.
 */

#pragma once

#include "MTLDevice.h"
#include "MTLLibrary.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Blending factors (subset) */
typedef enum {
    MTLBlendFactorZero             = 0,
    MTLBlendFactorOne              = 1,
    MTLBlendFactorSourceAlpha      = 4,
    MTLBlendFactorOneMinusSourceAlpha = 5,
} MTLBlendFactor;

typedef enum {
    MTLBlendOperationAdd            = 0,
    MTLBlendOperationSubtract       = 1,
} MTLBlendOperation;

/* Per-attachment blend state */
typedef struct {
    int              blendingEnabled;
    MTLBlendFactor   sourceRGBBlendFactor;
    MTLBlendFactor   destinationRGBBlendFactor;
    MTLBlendOperation rgbBlendOperation;
    MTLBlendFactor   sourceAlphaBlendFactor;
    MTLBlendFactor   destinationAlphaBlendFactor;
    MTLBlendOperation alphaBlendOperation;
    MTLPixelFormat   pixelFormat;
} MTLRenderPipelineColorAttachmentDescriptor;

/* Pipeline descriptor */
typedef struct {
    MTLFunction   vertexFunction;
    MTLFunction   fragmentFunction;
    MTLRenderPipelineColorAttachmentDescriptor colorAttachments[8];
    uint32_t      colorAttachmentCount;
    MTLPixelFormat depthAttachmentPixelFormat;
} MTLRenderPipelineDescriptor;

/*
 * purpose:  Compile and link a GL program from the two shader functions.
 * output:   New MTLRenderPipelineState or NULL; *error_out on link failure.
 */
MTLRenderPipelineState MTLDeviceNewRenderPipelineStateWithDescriptor(
    MTLDevice device,
    const MTLRenderPipelineDescriptor *descriptor,
    char **error_out);

/*
 * purpose:  Release GL program object.
 * sideEffects: glDeleteProgram.
 */
void MTLRenderPipelineStateRelease(MTLRenderPipelineState pso);

/* Internal: return GL program name. */
unsigned int MTLRenderPipelineStateGLProgram(MTLRenderPipelineState pso);

#ifdef __cplusplus
}
#endif
