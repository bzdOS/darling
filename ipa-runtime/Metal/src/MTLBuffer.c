/*
 * MTLBuffer.c — Metal buffer stub (GL_ARRAY_BUFFER backend)
 *
 * purpose:  Wrap a GL buffer object in an MTLBuffer handle.
 *           Persistent CPU mapping: buffer data lives in a heap shadow copy
 *           that is re-uploaded on MTLBufferDidModifyRange (simulates
 *           GL_MAP_WRITE_BIT without requiring GLES3 mapBuffer).
 * input:    Optional initial data pointer + length.
 * output:   Heap-allocated MTLBuffer_t or NULL.
 * sideEffects: glGenBuffers / glBufferData.
 */

#include <Metal/Metal.h>
#include <Metal/MTLBuffer.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <GLES2/gl2.h>

/* ------------------------------------------------------------------ */
/* Internal struct                                                     */
/* ------------------------------------------------------------------ */

struct MTLBuffer_t {
    GLuint  gl_buf;
    size_t  length;
    void   *shadow;   /* CPU-side mirror for persistent mapping */
};

/* ------------------------------------------------------------------ */
/* Internal constructor                                                */
/* ------------------------------------------------------------------ */

/*
 * purpose:  Allocate and optionally fill a GL buffer.
 * input:    bytes — initial data or NULL; length — byte count.
 * output:   New MTLBuffer_t or NULL.
 */
MTLBuffer mtl_buffer_create(MTLDevice device,
                              const void *bytes,
                              size_t length,
                              MTLResourceOptions options) {
    (void)device; (void)options;

    if (length == 0) return NULL;

    MTLBuffer buf = calloc(1, sizeof(struct MTLBuffer_t));
    if (!buf) return NULL;

    buf->length = length;
    buf->shadow = malloc(length);
    if (!buf->shadow) { free(buf); return NULL; }

    if (bytes) memcpy(buf->shadow, bytes, length);
    else       memset(buf->shadow, 0, length);

    glGenBuffers(1, &buf->gl_buf);
    if (!buf->gl_buf) {
        free(buf->shadow);
        free(buf);
        return NULL;
    }

    glBindBuffer(GL_ARRAY_BUFFER, buf->gl_buf);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)length, buf->shadow, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[Metal] glBufferData error 0x%x\n", err);
        glDeleteBuffers(1, &buf->gl_buf);
        free(buf->shadow);
        free(buf);
        return NULL;
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

size_t MTLBufferGetLength(MTLBuffer buf) {
    if (!buf) return 0;
    return buf->length;
}

void *MTLBufferGetContents(MTLBuffer buf) {
    if (!buf) return NULL;
    return buf->shadow;
}

/*
 * purpose:  Re-upload modified range to GL.
 * sideEffects: glBindBuffer + glBufferSubData.
 */
void MTLBufferDidModifyRange(MTLBuffer buf, size_t offset, size_t length) {
    if (!buf) return;
    if (offset + length > buf->length) return;
    glBindBuffer(GL_ARRAY_BUFFER, buf->gl_buf);
    glBufferSubData(GL_ARRAY_BUFFER,
                    (GLintptr)offset,
                    (GLsizeiptr)length,
                    (char *)buf->shadow + offset);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void MTLBufferRelease(MTLBuffer buf) {
    if (!buf) return;
    glDeleteBuffers(1, &buf->gl_buf);
    free(buf->shadow);
    free(buf);
}

unsigned int MTLBufferGLName(MTLBuffer buf) {
    if (!buf) return 0;
    return buf->gl_buf;
}
