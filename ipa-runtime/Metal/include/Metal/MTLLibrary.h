/*
 * MTLLibrary.h — Metal shader library stub (MSL→SPIR-V→GLSL pipeline)
 *
 * purpose:  Compile MSL source to a GL shader object via SPIRV-Cross.
 *           Pipeline: MSL source → spirv-cross CLI (--msl-to-spirv output is
 *           SPIR-V binary) → spirv-cross CLI (--glsl) → glShaderSource.
 *           Falls back to treating input as raw GLSL when spirv-cross is absent.
 * input:    MSL source string.
 * output:   MTLLibrary containing named MTLFunction handles.
 * sideEffects: Spawns spirv-cross subprocess; writes temp files under /tmp/mtl.
 */

#pragma once

#include "MTLDevice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shader stage hint (informational only for GL backend) */
typedef enum {
    MTLFunctionTypeVertex   = 1,
    MTLFunctionTypeFragment = 2,
    MTLFunctionTypeKernel   = 3,
} MTLFunctionType;

/*
 * purpose:  Look up a named function in a library.
 * input:    name — entry-point name as it appears in the MSL source.
 * output:   New MTLFunction or NULL if not found.
 * sideEffects: Compiles the associated GL shader on first access.
 */
MTLFunction MTLLibraryNewFunctionWithName(MTLLibrary lib, const char *name);

/*
 * purpose:  Release library (frees compiled GL shaders).
 * sideEffects: glDeleteShader for each compiled function.
 */
void MTLLibraryRelease(MTLLibrary lib);

/* MTLFunction */

/*
 * purpose:  Return the function's stage type.
 */
MTLFunctionType MTLFunctionGetType(MTLFunction fn);

/*
 * purpose:  Release function handle.
 */
void MTLFunctionRelease(MTLFunction fn);

/* Internal: return compiled GL shader object name. */
unsigned int MTLFunctionGLShader(MTLFunction fn);

#ifdef __cplusplus
}
#endif
