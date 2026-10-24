/*
 * MTLDevice.h — Metal device stub (OpenGL ES 2.0 backend via EGL/Mesa)
 *
 * purpose:  Expose MTLDevice creation/destruction and capability queries.
 * input:    none (singleton via MTLCreateSystemDefaultDevice)
 * output:   Opaque MTLDevice handle wrapping an EGL context.
 * sideEffects: Initialises EGL display + context on first call.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef struct MTLDevice_t        *MTLDevice;
typedef struct MTLCommandQueue_t  *MTLCommandQueue;
typedef struct MTLLibrary_t       *MTLLibrary;
typedef struct MTLFunction_t      *MTLFunction;
typedef struct MTLBuffer_t        *MTLBuffer;
typedef struct MTLTexture_t       *MTLTexture;
typedef struct MTLRenderPipeline_t *MTLRenderPipelineState;

/* Pixel formats (subset) */
typedef enum {
    MTLPixelFormatInvalid        = 0,
    MTLPixelFormatRGBA8Unorm     = 70,
    MTLPixelFormatBGRA8Unorm     = 80,
    MTLPixelFormatDepth32Float   = 252,
} MTLPixelFormat;

/* Resource storage modes */
typedef enum {
    MTLStorageModeShared  = 0,
    MTLStorageModePrivate = 2,
} MTLStorageMode;

/* Resource usage hints */
typedef enum {
    MTLResourceStorageModeShared  = MTLStorageModeShared  << 4,
    MTLResourceStorageModePrivate = MTLStorageModePrivate << 4,
} MTLResourceOptions;

/* Device capability info */
typedef struct {
    uint32_t max_texture_size;   /* GL_MAX_TEXTURE_SIZE */
    uint32_t max_buffer_length;  /* GL_MAX_VERTEX_ATTRIB_STRIDE proxy */
    int      supports_msaa;
} MTLDeviceCaps;

/*
 * purpose:  Create the default Metal device (single EGL context).
 * output:   New MTLDevice or NULL on failure.
 * sideEffects: Calls eglGetDisplay / eglCreateContext.
 */
MTLDevice MTLCreateSystemDefaultDevice(void);

/*
 * purpose:  Release device and destroy EGL context.
 * input:    device — must be non-NULL.
 * sideEffects: Calls eglDestroyContext + eglTerminate.
 */
void MTLDeviceRelease(MTLDevice device);

/*
 * purpose:  Query device capabilities.
 * input:    device — valid handle.
 * output:   Filled MTLDeviceCaps.
 */
MTLDeviceCaps MTLDeviceGetCaps(MTLDevice device);

/*
 * purpose:  Create a command queue on this device.
 * output:   New MTLCommandQueue or NULL.
 */
MTLCommandQueue MTLDeviceNewCommandQueue(MTLDevice device);

/*
 * purpose:  Compile MSL source into an MTLLibrary.
 * input:    source — null-terminated MSL string.
 * output:   New MTLLibrary or NULL; *error_out set on failure (caller frees).
 */
MTLLibrary MTLDeviceNewLibraryWithSource(MTLDevice device,
                                         const char *source,
                                         char **error_out);

/*
 * purpose:  Allocate an MTLBuffer backed by a GL buffer object.
 * input:    length — byte count; options — storage mode flags.
 * output:   New MTLBuffer or NULL.
 */
MTLBuffer MTLDeviceNewBufferWithLength(MTLDevice device,
                                       size_t length,
                                       MTLResourceOptions options);

/*
 * purpose:  Allocate an MTLBuffer pre-filled with data.
 * input:    bytes — source data; length — byte count.
 * output:   New MTLBuffer or NULL.
 */
MTLBuffer MTLDeviceNewBufferWithBytes(MTLDevice device,
                                      const void *bytes,
                                      size_t length,
                                      MTLResourceOptions options);

/*
 * purpose:  Allocate a 2-D MTLTexture.
 * input:    width/height — pixel dimensions; format — pixel format.
 * output:   New MTLTexture or NULL.
 */
MTLTexture MTLDeviceNewTexture2D(MTLDevice device,
                                  uint32_t width, uint32_t height,
                                  MTLPixelFormat format);

#ifdef __cplusplus
}
#endif
