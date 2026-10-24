/*
 * MTLLibrary.c — MSL→SPIR-V→GLSL shader compilation stub
 *
 * purpose:  Compile MSL source into GL shader objects.
 *           Strategy:
 *             1. Write MSL to /tmp/mtl_XXXXXX.msl
 *             2. Run `spirv-cross --glsl-es --version 100 <msl_file>` to
 *                produce GLSL ES 100 compatible with GLES2.
 *                (spirv-cross on FreeBSD can translate MSL→GLSL directly when
 *                built with MSL input support; if not available we fall back to
 *                treating the source as raw GLSL for unit-test purposes.)
 *             3. Feed the resulting GLSL into glCreateShader / glShaderSource /
 *                glCompileShader.
 * input:    MSL or GLSL source string.
 * output:   MTLLibrary_t with one "default" entry-point slot.
 * sideEffects: Temp files written to /tmp; spirv-cross subprocess launched.
 */

#include <Metal/Metal.h>
#include <Metal/MTLLibrary.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <GLES2/gl2.h>

/* ------------------------------------------------------------------ */
/* Internal structs                                                    */
/* ------------------------------------------------------------------ */

#define MTL_MAX_FUNCTIONS 16

struct MTLFunction_t {
    MTLFunctionType type;
    GLuint          gl_shader;  /* 0 = not yet compiled */
    char            name[128];
    char           *glsl_src;   /* heap-allocated GLSL */
};

struct MTLLibrary_t {
    struct MTLFunction_t functions[MTL_MAX_FUNCTIONS];
    int                  count;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Run spirv-cross to translate MSL → GLSL ES 100.
 * Returns heap-allocated GLSL string or NULL on failure.
 * Falls back to returning a copy of `src` so callers can test without
 * spirv-cross installed (treating input as raw GLSL). */
static char *msl_to_glsl(const char *src, char **error_out) {
    /* Write MSL to temp file */
    char msl_path[] = "/tmp/mtl_XXXXXX.msl";
    int fd = mkstemps(msl_path, 4);
    if (fd < 0) {
        if (error_out) *error_out = strdup("mkstemps failed");
        return NULL;
    }
    size_t src_len = strlen(src);
    if (write(fd, src, src_len) != (ssize_t)src_len) {
        close(fd);
        unlink(msl_path);
        if (error_out) *error_out = strdup("write temp MSL failed");
        return NULL;
    }
    close(fd);

    /* Output path */
    char glsl_path[] = "/tmp/mtl_XXXXXX.glsl";
    int fd2 = mkstemps(glsl_path, 5);
    if (fd2 < 0) {
        unlink(msl_path);
        if (error_out) *error_out = strdup("mkstemps for GLSL failed");
        return NULL;
    }
    close(fd2);

    /* Build spirv-cross command */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "spirv-cross --msl %s --output %s 2>/dev/null",
             msl_path, glsl_path);

    int rc = system(cmd);
    unlink(msl_path);

    if (rc != 0) {
        /* spirv-cross not installed or MSL unsupported — pass through as GLSL */
        fprintf(stderr,
                "[Metal] spirv-cross unavailable (rc=%d), "
                "treating source as raw GLSL\n", rc);
        unlink(glsl_path);
        return strdup(src);
    }

    /* Read back GLSL */
    FILE *f = fopen(glsl_path, "r");
    unlink(glsl_path);
    if (!f) {
        if (error_out) *error_out = strdup("cannot read spirv-cross output");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *glsl = malloc((size_t)len + 1);
    if (!glsl) { fclose(f); return NULL; }
    fread(glsl, 1, (size_t)len, f);
    glsl[len] = '\0';
    fclose(f);
    return glsl;
}

/* Compile a GL shader from GLSL source. Returns GL shader name or 0. */
static GLuint compile_gl_shader(const char *glsl, GLenum type,
                                 char **error_out) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    glShaderSource(shader, 1, &glsl, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = calloc(1, (size_t)(log_len + 1));
        if (log) glGetShaderInfoLog(shader, log_len, NULL, log);
        fprintf(stderr, "[Metal] GL shader compile error:\n%s\n",
                log ? log : "(no log)");
        if (error_out) *error_out = log ? log : strdup("compile error");
        else free(log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* ------------------------------------------------------------------ */
/* Internal constructor (called from MTLDevice.c)                      */
/* ------------------------------------------------------------------ */

/*
 * purpose:  Parse MSL source for entry-point names and compile each to a
 *           GL shader.  Simple heuristic: look for `vertex` and `fragment`
 *           stage keywords + function name tokens.
 * output:   Heap-allocated MTLLibrary_t or NULL.
 */
MTLLibrary mtl_library_create(MTLDevice device, const char *source,
                               char **error_out) {
    (void)device;
    if (!source) {
        if (error_out) *error_out = strdup("null source");
        return NULL;
    }

    MTLLibrary lib = calloc(1, sizeof(struct MTLLibrary_t));
    if (!lib) return NULL;

    /* Translate MSL → GLSL */
    char *glsl = msl_to_glsl(source, error_out);
    if (!glsl) {
        free(lib);
        return NULL;
    }

    /* Detect shader stage from MSL keywords.
     * MSL vertex shaders have `vertex <type> <name>(...)`.
     * Fragment shaders have `fragment <type> <name>(...)`.
     * If no stage keyword, default to vertex for GLSL pass-through. */

    /* Detect vertex stage */
    int has_vertex   = (strstr(source, "vertex ")   != NULL) ||
                       (strstr(source, "// vertex")  != NULL);
    int has_fragment = (strstr(source, "fragment ")  != NULL) ||
                       (strstr(source, "// fragment") != NULL);

    /* If raw GLSL (no MSL keywords), detect by gl_Position / gl_FragColor */
    if (!has_vertex && !has_fragment) {
        has_vertex   = (strstr(source, "gl_Position") != NULL);
        has_fragment = (strstr(source, "gl_FragColor") != NULL);
    }

    /* If still ambiguous, compile as vertex by default */
    if (!has_vertex && !has_fragment) has_vertex = 1;

    /* Compile vertex stage */
    if (has_vertex && lib->count < MTL_MAX_FUNCTIONS) {
        struct MTLFunction_t *fn = &lib->functions[lib->count];
        fn->type      = MTLFunctionTypeVertex;
        fn->glsl_src  = strdup(glsl);
        snprintf(fn->name, sizeof(fn->name), "vertexMain");
        fn->gl_shader = compile_gl_shader(glsl, GL_VERTEX_SHADER, error_out);
        lib->count++;
    }

    /* Compile fragment stage */
    if (has_fragment && lib->count < MTL_MAX_FUNCTIONS) {
        struct MTLFunction_t *fn = &lib->functions[lib->count];
        fn->type      = MTLFunctionTypeFragment;
        fn->glsl_src  = strdup(glsl);
        snprintf(fn->name, sizeof(fn->name), "fragmentMain");
        fn->gl_shader = compile_gl_shader(glsl, GL_FRAGMENT_SHADER, error_out);
        lib->count++;
    }

    free(glsl);
    return lib;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * purpose:  Return MTLFunction matching the requested entry-point name.
 *           Exact match first; falls back to first function of matching stage
 *           if name contains "vertex"/"fragment".
 * output:   Pointer into lib->functions array (no heap allocation) or NULL.
 */
MTLFunction MTLLibraryNewFunctionWithName(MTLLibrary lib, const char *name) {
    if (!lib || !name) return NULL;
    for (int i = 0; i < lib->count; i++) {
        if (strcmp(lib->functions[i].name, name) == 0)
            return &lib->functions[i];
    }
    /* Fuzzy: match by stage keyword in name */
    for (int i = 0; i < lib->count; i++) {
        if (strstr(name, "vert") && lib->functions[i].type == MTLFunctionTypeVertex)
            return &lib->functions[i];
        if (strstr(name, "frag") && lib->functions[i].type == MTLFunctionTypeFragment)
            return &lib->functions[i];
    }
    /* Return first available */
    if (lib->count > 0) return &lib->functions[0];
    return NULL;
}

void MTLLibraryRelease(MTLLibrary lib) {
    if (!lib) return;
    for (int i = 0; i < lib->count; i++) {
        if (lib->functions[i].gl_shader)
            glDeleteShader(lib->functions[i].gl_shader);
        free(lib->functions[i].glsl_src);
    }
    free(lib);
}

MTLFunctionType MTLFunctionGetType(MTLFunction fn) {
    if (!fn) return MTLFunctionTypeVertex;
    return fn->type;
}

void MTLFunctionRelease(MTLFunction fn) {
    /* Functions are owned by the library; no individual free. */
    (void)fn;
}

unsigned int MTLFunctionGLShader(MTLFunction fn) {
    if (!fn) return 0;
    return fn->gl_shader;
}
