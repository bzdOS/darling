/*
 * MTLTexture.c — Metal texture stub (GL_TEXTURE_2D backend)
 *
 * purpose:  Wrap a 2-D GL texture in an MTLTexture handle.
 *           Supports RGBA8 and BGRA8 pixel formats (GLES2 GL_RGBA).
 * input:    Width, height, MTLPixelFormat.
 * output:   Heap-allocated MTLTexture_t or NULL.
 * sideEffects: glGenTextures / glTexImage2D.
 */

#include <Metal/Metal.h>
#include <Metal/MTLTexture.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <GLES2/gl2.h>

/* ------------------------------------------------------------------ */
/* Internal struct                                                     */
/* ------------------------------------------------------------------ */

struct MTLTexture_t {
    GLuint         gl_tex;
    uint32_t       width;
    uint32_t       height;
    MTLPixelFormat format;
    GLenum         gl_internal; /* GL internal format */
    GLenum         gl_format;   /* GL pixel format    */
    GLenum         gl_type;     /* GL data type       */
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void mtl_pixel_format_to_gl(MTLPixelFormat fmt,
                                    GLenum *internal,
                                    GLenum *glfmt,
                                    GLenum *type) {
    switch (fmt) {
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatBGRA8Unorm:
        *internal = GL_RGBA;
        *glfmt    = GL_RGBA;
        *type     = GL_UNSIGNED_BYTE;
        break;
    default:
        *internal = GL_RGBA;
        *glfmt    = GL_RGBA;
        *type     = GL_UNSIGNED_BYTE;
        fprintf(stderr, "[Metal] unknown pixel format %d, defaulting to RGBA8\n",
                (int)fmt);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Internal constructor                                                */
/* ------------------------------------------------------------------ */

/*
 * purpose:  Allocate a blank 2-D GL texture.
 */
MTLTexture mtl_texture_create(MTLDevice device,
                                uint32_t width, uint32_t height,
                                MTLPixelFormat format) {
    (void)device;

    MTLTexture tex = calloc(1, sizeof(struct MTLTexture_t));
    if (!tex) return NULL;

    tex->width  = width;
    tex->height = height;
    tex->format = format;
    mtl_pixel_format_to_gl(format,
                            &tex->gl_internal,
                            &tex->gl_format,
                            &tex->gl_type);

    glGenTextures(1, &tex->gl_tex);
    if (!tex->gl_tex) { free(tex); return NULL; }

    glBindTexture(GL_TEXTURE_2D, tex->gl_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)tex->gl_internal,
                 (GLsizei)width, (GLsizei)height, 0,
                 tex->gl_format, tex->gl_type, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[Metal] glTexImage2D error 0x%x\n", err);
        glDeleteTextures(1, &tex->gl_tex);
        free(tex);
        return NULL;
    }
    return tex;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void MTLTextureReplaceRegion(MTLTexture tex,
                               uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height,
                               const void *bytes,
                               size_t bytesPerRow) {
    (void)bytesPerRow;
    if (!tex || !bytes) return;
    glBindTexture(GL_TEXTURE_2D, tex->gl_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    (GLint)x, (GLint)y,
                    (GLsizei)width, (GLsizei)height,
                    tex->gl_format, tex->gl_type, bytes);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void MTLTextureGetBytes(MTLTexture tex,
                         void *pixelBytes,
                         size_t bytesPerRow,
                         uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height) {
    if (!tex || !pixelBytes) return;
    /* GLES2 read-back requires a framebuffer attachment */
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex->gl_tex, 0);
    /* Read into a temporary full-row buffer then copy row-by-row if stride differs */
    size_t row_bytes = (size_t)width * 4; /* RGBA8 */
    if (bytesPerRow == row_bytes || bytesPerRow == 0) {
        glReadPixels((GLint)x, (GLint)y,
                     (GLsizei)width, (GLsizei)height,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixelBytes);
    } else {
        unsigned char *tmp = malloc(row_bytes * height);
        if (tmp) {
            glReadPixels((GLint)x, (GLint)y,
                         (GLsizei)width, (GLsizei)height,
                         GL_RGBA, GL_UNSIGNED_BYTE, tmp);
            for (uint32_t row = 0; row < height; row++)
                memcpy((char *)pixelBytes + row * bytesPerRow,
                       tmp + row * row_bytes, row_bytes);
            free(tmp);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
}

uint32_t MTLTextureGetWidth(MTLTexture tex)  { return tex ? tex->width  : 0; }
uint32_t MTLTextureGetHeight(MTLTexture tex) { return tex ? tex->height : 0; }
MTLPixelFormat MTLTextureGetPixelFormat(MTLTexture tex) {
    return tex ? tex->format : MTLPixelFormatInvalid;
}

void MTLTextureRelease(MTLTexture tex) {
    if (!tex) return;
    glDeleteTextures(1, &tex->gl_tex);
    free(tex);
}

unsigned int MTLTextureGLName(MTLTexture tex) {
    if (!tex) return 0;
    return tex->gl_tex;
}
