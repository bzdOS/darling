/*
 * MTLDevice.c — Metal device stub implementation (EGL + OpenGL ES 2.0)
 *
 * purpose:  Create/destroy an EGL pbuffer context used by all Metal objects on
 *           this device.  On FreeBSD with Mesa tries EGL_DEFAULT_DISPLAY first,
 *           then falls back to EGL_PLATFORM_SURFACELESS_MESA for headless servers.
 * input:    none (singleton factory)
 * output:   Allocated MTLDevice_t or NULL on EGL error.
 * sideEffects: eglGetDisplay / eglInitialize / eglCreateContext + pbuffer surface.
 */

#include <Metal/Metal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* EGL headers — provided by mesa-libs on FreeBSD */
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* GLES2 headers */
#include <GLES2/gl2.h>

/* EGL_PLATFORM_SURFACELESS_MESA — defined in eglext.h, but guard just in case */
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

/* ------------------------------------------------------------------ */
/* Internal device struct                                              */
/* ------------------------------------------------------------------ */

struct MTLDevice_t {
    EGLDisplay  egl_display;
    EGLContext  egl_context;
    EGLSurface  egl_surface;   /* 1×1 pbuffer for offscreen work */
    EGLConfig   egl_config;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void log_egl_error(const char *where) {
    fprintf(stderr, "[Metal] EGL error 0x%x at %s\n", eglGetError(), where);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * purpose:  Create system default Metal device.
 * output:   Heap-allocated MTLDevice_t or NULL.
 * sideEffects: Initialises EGL display + context + 1×1 pbuffer.
 */
MTLDevice MTLCreateSystemDefaultDevice(void) {
    MTLDevice dev = calloc(1, sizeof(struct MTLDevice_t));
    if (!dev) return NULL;

    /* Try EGL_DEFAULT_DISPLAY first (works when X11/Wayland display is available) */
    dev->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    if (dev->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(dev->egl_display, &major, &minor)) {
        /*
         * Headless fallback: EGL_PLATFORM_SURFACELESS_MESA lets Mesa use
         * swrast (llvmpipe) without any display server.  This is the normal
         * path on server VMs (FreeBSD 185 dev VM, CI, etc.).
         */
        fprintf(stderr, "[Metal] EGL_DEFAULT_DISPLAY unavailable, "
                        "trying EGL_PLATFORM_SURFACELESS_MESA\n");
        PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)
            eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (!eglGetPlatformDisplayEXT) {
            fprintf(stderr, "[Metal] eglGetPlatformDisplayEXT unavailable\n");
            free(dev);
            return NULL;
        }
        dev->egl_display = eglGetPlatformDisplayEXT(
            EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
        if (dev->egl_display == EGL_NO_DISPLAY) {
            log_egl_error("eglGetPlatformDisplayEXT(surfaceless)");
            free(dev);
            return NULL;
        }
        if (!eglInitialize(dev->egl_display, &major, &minor)) {
            log_egl_error("eglInitialize(surfaceless)");
            eglTerminate(dev->egl_display);
            free(dev);
            return NULL;
        }
    }
    fprintf(stderr, "[Metal] EGL %d.%d initialised\n", major, minor);

    /* Choose config: GLES2, RGBA8, depth 24, pbuffer */
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };
    EGLint num_config = 0;
    if (!eglChooseConfig(dev->egl_display, attribs, &dev->egl_config, 1, &num_config)
        || num_config == 0) {
        log_egl_error("eglChooseConfig");
        eglTerminate(dev->egl_display);
        free(dev);
        return NULL;
    }

    /* Create GLES2 context */
    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    eglBindAPI(EGL_OPENGL_ES_API);
    dev->egl_context = eglCreateContext(dev->egl_display, dev->egl_config,
                                         EGL_NO_CONTEXT, ctx_attribs);
    if (dev->egl_context == EGL_NO_CONTEXT) {
        log_egl_error("eglCreateContext");
        eglTerminate(dev->egl_display);
        free(dev);
        return NULL;
    }

    /* 1×1 pbuffer surface so the context can be made current without a window */
    const EGLint pbuf_attribs[] = {
        EGL_WIDTH,  1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    dev->egl_surface = eglCreatePbufferSurface(dev->egl_display,
                                                dev->egl_config,
                                                pbuf_attribs);
    if (dev->egl_surface == EGL_NO_SURFACE) {
        log_egl_error("eglCreatePbufferSurface");
        eglDestroyContext(dev->egl_display, dev->egl_context);
        eglTerminate(dev->egl_display);
        free(dev);
        return NULL;
    }

    if (!eglMakeCurrent(dev->egl_display, dev->egl_surface,
                         dev->egl_surface, dev->egl_context)) {
        log_egl_error("eglMakeCurrent");
        eglDestroySurface(dev->egl_display, dev->egl_surface);
        eglDestroyContext(dev->egl_display, dev->egl_context);
        eglTerminate(dev->egl_display);
        free(dev);
        return NULL;
    }

    fprintf(stderr, "[Metal] GL renderer: %s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "[Metal] GL version:  %s\n", glGetString(GL_VERSION));
    return dev;
}

/*
 * purpose:  Destroy device and tear down EGL.
 * sideEffects: eglMakeCurrent(none) + destroy surface/context/display.
 */
void MTLDeviceRelease(MTLDevice device) {
    if (!device) return;
    eglMakeCurrent(device->egl_display,
                   EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(device->egl_display, device->egl_surface);
    eglDestroyContext(device->egl_display, device->egl_context);
    eglTerminate(device->egl_display);
    free(device);
}

/*
 * purpose:  Query device capabilities from GL.
 * output:   MTLDeviceCaps struct filled from glGetIntegerv.
 */
MTLDeviceCaps MTLDeviceGetCaps(MTLDevice device) {
    MTLDeviceCaps caps = {0};
    (void)device;
    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    caps.max_texture_size = (uint32_t)v;
    caps.max_buffer_length = 256 * 1024 * 1024; /* 256 MiB — conservative */
    caps.supports_msaa = 0;  /* GLES2 base profile: no MSAA */
    return caps;
}

/* ------------------------------------------------------------------ */
/* Forward declarations implemented in other translation units         */
/* ------------------------------------------------------------------ */

/* MTLCommandQueue is in MTLCommandQueue.c */
/* MTLLibrary      is in MTLLibrary.c      */
/* MTLBuffer       is in MTLBuffer.c       */
/* MTLTexture      is in MTLTexture.c      */
/* MTLRenderPSO    is in MTLRenderPipeline.c (via MTLRenderPipeline.h) */

/*
 * purpose:  Create a new command queue for this device.
 * output:   Heap-allocated MTLCommandQueue_t or NULL.
 */
MTLCommandQueue MTLDeviceNewCommandQueue(MTLDevice device) {
    /* Implemented in MTLCommandQueue.c; declared extern there. */
    extern MTLCommandQueue mtl_command_queue_create(MTLDevice);
    return mtl_command_queue_create(device);
}

/*
 * purpose:  Compile MSL source into a library.
 * output:   MTLLibrary or NULL.
 */
MTLLibrary MTLDeviceNewLibraryWithSource(MTLDevice device,
                                          const char *source,
                                          char **error_out) {
    extern MTLLibrary mtl_library_create(MTLDevice, const char *, char **);
    return mtl_library_create(device, source, error_out);
}

/*
 * purpose:  Allocate empty GL buffer of given length.
 */
MTLBuffer MTLDeviceNewBufferWithLength(MTLDevice device,
                                        size_t length,
                                        MTLResourceOptions options) {
    extern MTLBuffer mtl_buffer_create(MTLDevice, const void *, size_t,
                                        MTLResourceOptions);
    return mtl_buffer_create(device, NULL, length, options);
}

/*
 * purpose:  Allocate GL buffer pre-filled with caller data.
 */
MTLBuffer MTLDeviceNewBufferWithBytes(MTLDevice device,
                                       const void *bytes,
                                       size_t length,
                                       MTLResourceOptions options) {
    extern MTLBuffer mtl_buffer_create(MTLDevice, const void *, size_t,
                                        MTLResourceOptions);
    return mtl_buffer_create(device, bytes, length, options);
}

/*
 * purpose:  Allocate a 2-D GL texture.
 */
MTLTexture MTLDeviceNewTexture2D(MTLDevice device,
                                   uint32_t width, uint32_t height,
                                   MTLPixelFormat format) {
    extern MTLTexture mtl_texture_create(MTLDevice, uint32_t, uint32_t,
                                          MTLPixelFormat);
    return mtl_texture_create(device, width, height, format);
}
