/*
 * MTLCommandQueue.c — Metal command queue + buffer + render encoder stubs
 *
 * purpose:  Translate Metal recording API to immediate-mode OpenGL ES 2.0.
 *           Since GLES2 has no deferred command buffers, Metal "commit" maps
 *           to glFlush and the encoder issues GL calls inline.
 * input:    MTLDevice + pass descriptor.
 * output:   Draws appear in the GL framebuffer bound at commit time.
 * sideEffects: GL state changes (program, VBOs, FBO, viewport).
 */

#include <Metal/Metal.h>
#include <Metal/MTLCommandQueue.h>
#include <Metal/MTLRenderPipeline.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <GLES2/gl2.h>

/* ------------------------------------------------------------------ */
/* Internal structs                                                    */
/* ------------------------------------------------------------------ */

struct MTLCommandQueue_t {
    MTLDevice device;
    GLuint    default_fbo;  /* 0 = window framebuffer */
};

struct MTLCommandBuffer_t {
    MTLCommandQueue queue;
};

#define MAX_VERTEX_BUFFERS 8

struct MTLRenderCommandEncoder_t {
    MTLCommandBuffer        buf;
    MTLRenderPipelineState  pso;
    GLuint                  fbo;           /* bound FBO */
    GLuint                  vertex_bufs[MAX_VERTEX_BUFFERS];
    uint32_t                vertex_offsets[MAX_VERTEX_BUFFERS];
};

/* ------------------------------------------------------------------ */
/* Internal constructor (called from MTLDevice.c)                      */
/* ------------------------------------------------------------------ */

MTLCommandQueue mtl_command_queue_create(MTLDevice device) {
    MTLCommandQueue q = calloc(1, sizeof(struct MTLCommandQueue_t));
    if (!q) return NULL;
    q->device = device;
    return q;
}

/* ------------------------------------------------------------------ */
/* Command queue                                                       */
/* ------------------------------------------------------------------ */

void MTLCommandQueueRelease(MTLCommandQueue queue) {
    free(queue);
}

MTLCommandBuffer MTLCommandQueueCommandBuffer(MTLCommandQueue queue) {
    if (!queue) return NULL;
    MTLCommandBuffer buf = calloc(1, sizeof(struct MTLCommandBuffer_t));
    if (!buf) return NULL;
    buf->queue = queue;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Command buffer                                                      */
/* ------------------------------------------------------------------ */

MTLRenderCommandEncoder MTLCommandBufferRenderCommandEncoderWithDescriptor(
    MTLCommandBuffer buf,
    const MTLRenderPassDescriptor *desc)
{
    if (!buf || !desc) return NULL;

    MTLRenderCommandEncoder enc = calloc(1, sizeof(struct MTLRenderCommandEncoder_t));
    if (!enc) return NULL;
    enc->buf = buf;

    /* Resolve framebuffer */
    if (desc->colorAttachmentCount > 0 &&
        desc->colorAttachments[0].texture != NULL)
    {
        /* Render into the given MTLTexture via a framebuffer object */
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               MTLTextureGLName(desc->colorAttachments[0].texture),
                               0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[Metal] FBO incomplete: 0x%x\n", status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            free(enc);
            return NULL;
        }
        enc->fbo = fbo;
    } else {
        /* Default window framebuffer */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        enc->fbo = 0;
    }

    /* Apply clear action */
    if (desc->colorAttachmentCount > 0 &&
        desc->colorAttachments[0].loadAction == MTLLoadActionClear)
    {
        MTLClearColor cc = desc->colorAttachments[0].clearColor;
        glClearColor((GLfloat)cc.red,  (GLfloat)cc.green,
                     (GLfloat)cc.blue, (GLfloat)cc.alpha);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    return enc;
}

void MTLCommandBufferCommit(MTLCommandBuffer buf) {
    (void)buf;
    glFlush();
}

void MTLCommandBufferWaitUntilCompleted(MTLCommandBuffer buf) {
    (void)buf;
    glFinish();
}

void MTLCommandBufferRelease(MTLCommandBuffer buf) {
    free(buf);
}

/* ------------------------------------------------------------------ */
/* Render command encoder                                              */
/* ------------------------------------------------------------------ */

void MTLRenderCommandEncoderSetViewport(MTLRenderCommandEncoder enc,
                                         MTLViewport vp)
{
    if (!enc) return;
    glViewport((GLint)vp.originX,  (GLint)vp.originY,
               (GLsizei)vp.width,  (GLsizei)vp.height);
    glDepthRangef((GLfloat)vp.znear, (GLfloat)vp.zfar);
}

void MTLRenderCommandEncoderSetRenderPipelineState(
    MTLRenderCommandEncoder enc, MTLRenderPipelineState pso)
{
    if (!enc) return;
    enc->pso = pso;
    if (pso) glUseProgram(MTLRenderPipelineStateGLProgram(pso));
}

void MTLRenderCommandEncoderSetVertexBuffer(MTLRenderCommandEncoder enc,
                                             MTLBuffer buf,
                                             uint32_t offset,
                                             uint32_t index)
{
    if (!enc || index >= MAX_VERTEX_BUFFERS) return;
    enc->vertex_bufs[index]    = buf ? MTLBufferGLName(buf) : 0;
    enc->vertex_offsets[index] = offset;
}

void MTLRenderCommandEncoderSetFragmentTexture(MTLRenderCommandEncoder enc,
                                                MTLTexture tex,
                                                uint32_t index)
{
    if (!enc) return;
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture(GL_TEXTURE_2D, tex ? MTLTextureGLName(tex) : 0);
    /* Set sampler uniform if program is bound */
    if (enc->pso) {
        char uname[32];
        snprintf(uname, sizeof(uname), "texture%u", index);
        GLint loc = glGetUniformLocation(MTLRenderPipelineStateGLProgram(enc->pso),
                                         uname);
        if (loc >= 0) glUniform1i(loc, (GLint)index);
    }
}

/*
 * purpose:  Issue a non-indexed draw.  Vertex buffer at index 0 is bound as
 *           GL_ARRAY_BUFFER; attribute layout assumed single float4 (position)
 *           at location 0 for the simple triangle test.
 */
void MTLRenderCommandEncoderDrawPrimitives(MTLRenderCommandEncoder enc,
                                            MTLPrimitiveType type,
                                            uint32_t vertexStart,
                                            uint32_t vertexCount)
{
    if (!enc) return;

    static const GLenum prim_map[] = {
        GL_POINTS,          /* MTLPrimitiveTypePoint */
        GL_LINES,           /* MTLPrimitiveTypeLine */
        GL_LINE_STRIP,      /* MTLPrimitiveTypeLineStrip */
        GL_TRIANGLES,       /* MTLPrimitiveTypeTriangle */
        GL_TRIANGLE_STRIP,  /* MTLPrimitiveTypeTriangleStrip */
    };
    GLenum gl_prim = (type <= MTLPrimitiveTypeTriangleStrip)
                     ? prim_map[type] : GL_TRIANGLES;

    /* Bind vertex buffer 0 and set up attribute 0 (vec4 position) */
    if (enc->vertex_bufs[0]) {
        glBindBuffer(GL_ARRAY_BUFFER, enc->vertex_bufs[0]);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0,
                              (void *)(uintptr_t)enc->vertex_offsets[0]);
    }

    glDrawArrays(gl_prim, (GLint)vertexStart, (GLsizei)vertexCount);

    if (enc->vertex_bufs[0]) {
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void MTLRenderCommandEncoderDrawIndexedPrimitives(
    MTLRenderCommandEncoder enc,
    MTLPrimitiveType type,
    uint32_t indexCount,
    MTLIndexType indexType,
    MTLBuffer indexBuffer,
    uint32_t indexBufferOffset)
{
    if (!enc) return;

    static const GLenum prim_map[] = {
        GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP,
    };
    GLenum gl_prim = (type <= MTLPrimitiveTypeTriangleStrip)
                     ? prim_map[type] : GL_TRIANGLES;
    GLenum gl_idx = (indexType == MTLIndexTypeUInt32)
                    ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

    if (enc->vertex_bufs[0]) {
        glBindBuffer(GL_ARRAY_BUFFER, enc->vertex_bufs[0]);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0,
                              (void *)(uintptr_t)enc->vertex_offsets[0]);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                 indexBuffer ? MTLBufferGLName(indexBuffer) : 0);
    glDrawElements(gl_prim, (GLsizei)indexCount, gl_idx,
                   (void *)(uintptr_t)indexBufferOffset);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (enc->vertex_bufs[0]) {
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void MTLRenderCommandEncoderEndEncoding(MTLRenderCommandEncoder enc) {
    if (!enc) return;
    /* Restore default framebuffer if we allocated one */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MTLRenderCommandEncoderRelease(MTLRenderCommandEncoder enc) {
    if (!enc) return;
    if (enc->fbo) glDeleteFramebuffers(1, &enc->fbo);
    free(enc);
}
