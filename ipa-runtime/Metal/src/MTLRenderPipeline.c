/*
 * MTLRenderPipeline.c — Metal render pipeline state stub (GL program backend)
 *
 * purpose:  Link a vertex + fragment GL shader into a GL program and cache it
 *           as an MTLRenderPipelineState.
 * input:    MTLRenderPipelineDescriptor with vertex/fragment MTLFunction.
 * output:   Heap-allocated MTLRenderPipeline_t or NULL.
 * sideEffects: glCreateProgram / glAttachShader / glLinkProgram.
 */

#include <Metal/Metal.h>
#include <Metal/MTLRenderPipeline.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <GLES2/gl2.h>

/* ------------------------------------------------------------------ */
/* Internal struct                                                     */
/* ------------------------------------------------------------------ */

struct MTLRenderPipeline_t {
    GLuint gl_program;
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * purpose:  Compile and link vertex + fragment shaders into a GL program.
 * output:   New MTLRenderPipelineState or NULL on link error.
 */
MTLRenderPipelineState MTLDeviceNewRenderPipelineStateWithDescriptor(
    MTLDevice device,
    const MTLRenderPipelineDescriptor *desc,
    char **error_out)
{
    (void)device;
    if (!desc) { if (error_out) *error_out = strdup("null descriptor"); return NULL; }
    if (!desc->vertexFunction || !desc->fragmentFunction) {
        if (error_out) *error_out = strdup("vertex/fragment function required");
        return NULL;
    }

    GLuint prog = glCreateProgram();
    if (!prog) { if (error_out) *error_out = strdup("glCreateProgram failed"); return NULL; }

    GLuint vert = MTLFunctionGLShader(desc->vertexFunction);
    GLuint frag = MTLFunctionGLShader(desc->fragmentFunction);

    if (vert) glAttachShader(prog, vert);
    if (frag) glAttachShader(prog, frag);

    /* Bind attribute 0 to "position" before linking */
    glBindAttribLocation(prog, 0, "position");
    glBindAttribLocation(prog, 1, "texcoord");
    glBindAttribLocation(prog, 2, "color");

    glLinkProgram(prog);

    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        GLint log_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
        char *log = calloc(1, (size_t)(log_len + 1));
        if (log) glGetProgramInfoLog(prog, log_len, NULL, log);
        fprintf(stderr, "[Metal] GL program link error:\n%s\n",
                log ? log : "(no log)");
        if (error_out) *error_out = log ? log : strdup("link error");
        else free(log);
        glDeleteProgram(prog);
        return NULL;
    }

    MTLRenderPipelineState pso = calloc(1, sizeof(struct MTLRenderPipeline_t));
    if (!pso) { glDeleteProgram(prog); return NULL; }
    pso->gl_program = prog;
    return pso;
}

void MTLRenderPipelineStateRelease(MTLRenderPipelineState pso) {
    if (!pso) return;
    glDeleteProgram(pso->gl_program);
    free(pso);
}

unsigned int MTLRenderPipelineStateGLProgram(MTLRenderPipelineState pso) {
    if (!pso) return 0;
    return pso->gl_program;
}
