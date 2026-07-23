/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include "unordered_map/unordered_map.h"
#include "vgpu_shaderconv/shaderconv.h"
#include "glsl_optimizer/src/code/c_wrapper.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "string_utils.h"
#include "egl.h"
#include "proc.h"

typedef struct {
    GLenum shader_type;
    const GLchar* source;
} shader_info_t;

typedef struct {
    GLuint frag_shader;
    GLchar* colorbindings[MAX_DRAWBUFFERS];
} program_info_t;

GLuint glCreateProgram(void) {
    if(!current_context) return 0;
    GLuint phys_program = es3_functions.glCreateProgram();
    if(phys_program == 0) return phys_program;
    program_info_t *prog_info = calloc(1, sizeof(program_info_t));
    if(prog_info == NULL) {
        printf("LTWShdrWp: failed to allocate program_info\n");
        abort();
    }
    unordered_map_put(current_context->program_map, (void*)phys_program, prog_info);
    return phys_program;
}

void glDeleteProgram(GLuint program) {
    if(!current_context) return;
    es3_functions.glDeleteProgram(program);
    program_info_t *old_programinfo = unordered_map_remove(current_context->program_map, (void*)program);
    if(old_programinfo == NULL) return;
    for(GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        const GLchar* binding = old_programinfo->colorbindings[i];
        if(binding != NULL) free((void*)binding);
    }
    free(old_programinfo);
}

void glAttachShader( 	GLuint program,
                        GLuint shader) {
    if(!current_context) return;
    es3_functions.glAttachShader(program, shader);
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)program);
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)shader);
    if(program_info == NULL || shader_info == NULL || shader_info->shader_type != GL_FRAGMENT_SHADER) return;
    program_info->frag_shader = shader;
}

void glBindFragDataLocation( 	GLuint program,
                                GLuint colorNumber,
                                const char * name) {
    if(!current_context) return;
    program_info_t *program_info = unordered_map_get(current_context->program_map, (void*)program);
    if(program_info == NULL || colorNumber >= MAX_DRAWBUFFERS) return;
    // Insert binding name at the specific index
    GLchar** pname = &program_info->colorbindings[colorNumber];
    if(asprintf(pname, "%s", name) == -1) {
        *pname = NULL;
    }
}

void glGetShaderiv(GLuint shader, GLuint pname, GLint* params) {
    if(!current_context) return;
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)shader);
    if(shader_info != NULL && shader_info->shader_type == GL_FRAGMENT_SHADER && pname == GL_COMPILE_STATUS) {
        // HACK: ignore compile results for frag shaders, as some drivers may not compile them without explicit fragouts
        // (which we add at link-time)
        *params = GL_TRUE;
        return;
    }
    es3_functions.glGetShaderiv(shader, pname, params);
}

static void insert_fragout_pos(char* source, int* size, const char* name, GLuint pos) {
    char src_string[256] = { 0 };
    char dst_string[256] = { 0 };
    snprintf(src_string, sizeof(src_string), "/* LTW INSERT LOCATION %s LTW */", name);
    snprintf(dst_string, sizeof(dst_string), "layout(location = %u) ", pos);
    gl4es_inplace_replace_simple(source, size, src_string, dst_string);
}

void glLinkProgram(GLuint program) {
    if(!current_context) return;
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)program);
    if(program_info == NULL || program_info->frag_shader == 0) {
        // Don't have any fragment shader to patch the locations in, fall through.
        goto fallthrough;
    }
    shader_info_t *shader = unordered_map_get(current_context->shader_map, (void*)program_info->frag_shader);
    if(shader == NULL) {
        printf("LTWShdrWp: failed to patch frag data location due to missing shader info\n");
        goto fallthrough;
    }
    int nsrc_size = (int)(strlen(shader->source) + 1);
    char* new_source = malloc(nsrc_size);
    memcpy(new_source, shader->source, nsrc_size);
    bool changesMade = false;
    for(GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        const char* colorbind = program_info->colorbindings[i];
        if(colorbind == NULL) continue;
        insert_fragout_pos(new_source, &nsrc_size, colorbind, i);
        changesMade = true;
    }
    if(!changesMade) {
        free(new_source);
        goto fallthrough;
    }else {
        //printf("\n\n\nShader Result POST PATCH\n%s\n\n\n", new_source);
    }
    const GLchar* const_source = (const GLchar*)new_source;
    GLuint patched_shader = es3_functions.glCreateShader(GL_FRAGMENT_SHADER);
    if(patched_shader == 0) {
        free(new_source);
        printf("LTWShdrWp: failed to initialize patched shader\n");
        goto fallthrough;
    }
    es3_functions.glShaderSource(patched_shader, 1, &const_source, NULL);
    es3_functions.glCompileShader(patched_shader);
    free(new_source);
    GLint compileStatus;
    es3_functions.glGetShaderiv(patched_shader, GL_COMPILE_STATUS, &compileStatus);
    if(compileStatus != GL_TRUE) {
        GLint logSize;
        es3_functions.glGetShaderiv(patched_shader, GL_INFO_LOG_LENGTH, &logSize);
        GLchar log[logSize];
        es3_functions.glGetShaderInfoLog(patched_shader, logSize, NULL, log);
        printf("LTWShdrWp: failed to compile patched fragment shader, using default. Log:\n\n%s\n\nShader content:\n\n%s\n\n", log, const_source);
        goto fallthrough;
    }
    es3_functions.glDetachShader(program, program_info->frag_shader);
    es3_functions.glAttachShader(program, patched_shader);
    es3_functions.glLinkProgram(program);
    es3_functions.glDeleteShader(patched_shader);
    return;
    fallthrough:
    es3_functions.glLinkProgram(program);
}

GLuint glCreateShader(GLenum shaderType) {
    if(!current_context) return 0;
    GLuint phys_shader = es3_functions.glCreateShader(shaderType);
    if(phys_shader == 0) return 0;
    shader_info_t* info_struct = calloc(1, sizeof(shader_info_t));
    if(info_struct == NULL) {
        printf("LTWShdrWp: failed to allocate shader_info\n");
        abort();
    }
    info_struct->shader_type = shaderType;
    unordered_map_put(current_context->shader_map, (void*)phys_shader, info_struct);
    return phys_shader;
}

void glDeleteShader(GLuint shader) {
    if(!current_context) return;
    es3_functions.glDeleteShader(shader);
    shader_info_t * old_shaderinfo = unordered_map_remove(current_context->shader_map, (void*)shader);
    if(old_shaderinfo == NULL) return;
    if(old_shaderinfo->source != NULL) free((void*)old_shaderinfo->source);
    free(old_shaderinfo);
}

// ---- texture-buffer shader lowering (ES 3.0 emulation) ---------------------
// When the host GLES backend lacks GL_EXT_texture_buffer, GLSL ES 3.00 has no
// samplerBuffer/isamplerBuffer/usamplerBuffer types at all, so a shader that
// uses them (e.g. MC 26.2's cloud shader: `uniform isamplerBuffer CloudFaces;
// texelFetch(CloudFaces, i)`) cannot compile. LTW emulates buffer textures
// with 2D textures at runtime (see main.c), so here we lower the shader types:
//   isamplerBuffer  -> isampler2D
//   usamplerBuffer  -> usampler2D
//   samplerBuffer   -> sampler2D
// and inject 1-arg texelFetch/textureSize overloads that wrap the integer
// coordinate into an ivec2 + lod 0, matching the 2D builtin signatures:
//   texelFetch(s, int x)        -> texelFetch(s, ivec2(x,0), 0)
//   textureSize(s)              -> textureSize(s, 0).x
// The glsl_optimizer then parses these as ordinary sampler2D usage and the ES
// 3.0 driver accepts them. Whole-word matching avoids touching substrings.

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Replaces every whole-word occurrence of `from` with `to` in `src`, writing
// into the pre-sized `out`. Returns the number of replacements. `seen` arrays
// are scanned with word boundaries so e.g. "samplerBuffer" does not match
// inside "isamplerBuffer" (handled by ordering: longer keys first).
static int replace_word(const char* src, char* out, const char* from, const char* to) {
    size_t flen = strlen(from), tlen = strlen(to);
    int count = 0;
    const char* sp = src;
    char* op = out;
    while(*sp) {
        if(strncmp(sp, from, flen) == 0) {
            char prev = (sp == src) ? '\0' : sp[-1];
            char next = sp[flen];
            if(!is_ident_char(prev) && !is_ident_char(next)) {
                memcpy(op, to, tlen);
                op += tlen;
                sp += flen;
                count++;
                continue;
            }
        }
        *op++ = *sp++;
    }
    *op = 0;
    return count;
}

// Returns a freshly malloc'd lowered source, or NULL if the source contains no
// buffer-sampler usage (caller should keep the original). Caller frees result.
static GLchar* lower_sampler_buffers(const GLchar* src) {
    if(strstr(src, "samplerBuffer") == NULL) return NULL;

    // Two-pass: first on a temporary to count, then build the final string with
    // the overload prelude prepended. Replacements happen longest-first so
    // isamplerBuffer/usamplerBuffer are handled before the bare samplerBuffer.
    // Phase 1: count each variant's occurrences to know which overloads to add.
    // We do a single replacement pass into a temp buffer, then prepend prelude.
    size_t srclen = strlen(src);
    char* tmp = malloc(srclen + 1);
    if(!tmp) return NULL;

    int saw_i = 0, saw_u = 0, saw_plain = 0;
    // isamplerBuffer -> isampler2D (longest first)
    int n = replace_word(src, tmp, "isamplerBuffer", "isampler2D");
    saw_i = n > 0;
    // usamplerBuffer -> usampler2D
    char* tmp2 = malloc(strlen(tmp) + 1);
    if(!tmp2) { free(tmp); return NULL; }
    n = replace_word(tmp, tmp2, "usamplerBuffer", "usampler2D");
    saw_u = n > 0;
    // samplerBuffer -> sampler2D (now only bare occurrences remain)
    // Size upper bound: every match shrinks (samplerBuffer=13 -> sampler2D=9),
    // so strlen(tmp2) is a safe upper bound for the replaced buffer.
    char* tmp3 = malloc(strlen(tmp2) + 1);
    if(!tmp3) { free(tmp); free(tmp2); return NULL; }
    n = replace_word(tmp2, tmp3, "samplerBuffer", "sampler2D");
    saw_plain = n > 0;
    free(tmp);
    free(tmp2);

    // Build the prelude of overload wrappers, only for types actually present.
    // Inserted right after the #version directive (or at the very start).
    char prelude[768];
    prelude[0] = 0;
    if(saw_plain) {
        strcat(prelude, "vec4 texelFetch(sampler2D _ltw_s,int _ltw_c){return texelFetch(_ltw_s,ivec2(_ltw_c,0),0);}\n");
        strcat(prelude, "int textureSize(sampler2D _ltw_s){return textureSize(_ltw_s,0).x;}\n");
    }
    if(saw_i) {
        strcat(prelude, "ivec4 texelFetch(isampler2D _ltw_s,int _ltw_c){return texelFetch(_ltw_s,ivec2(_ltw_c,0),0);}\n");
        strcat(prelude, "int textureSize(isampler2D _ltw_s){return textureSize(_ltw_s,0).x;}\n");
    }
    if(saw_u) {
        strcat(prelude, "uvec4 texelFetch(usampler2D _ltw_s,int _ltw_c){return texelFetch(_ltw_s,ivec2(_ltw_c,0),0);}\n");
        strcat(prelude, "int textureSize(usampler2D _ltw_s){return textureSize(_ltw_s,0).x;}\n");
    }

    // Find insertion point: end of the leading #version line (if any).
    const char* vp = tmp3;
    const char* insert_at = tmp3;
    if(strncmp(tmp3, "#version", 8) == 0) {
        const char* nl = strchr(tmp3, '\n');
        if(nl) insert_at = nl + 1;
        else insert_at = tmp3 + strlen(tmp3);
    }

    size_t head = (size_t)(insert_at - tmp3);
    size_t taillen = strlen(insert_at);
    size_t plen = strlen(prelude);
    GLchar* result = malloc(head + plen + taillen + 1);
    if(!result) { free(tmp3); return NULL; }
    memcpy(result, tmp3, head);
    memcpy(result + head, prelude, plen);
    memcpy(result + head + plen, insert_at, taillen);
    result[head + plen + taillen] = 0;
    free(tmp3);
    return result;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) {
    if(!current_context) return;
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)shader);
    if(shader_info == NULL) {
        printf("LTWShdrWp: shader_info missing for shader %u\n", shader);
        es3_functions.glShaderSource(shader, count, string, length);
        return;
    }

    size_t target_length = 0;
#define SRC_LEN(x) length != NULL ? length[x] : strlen(string[x])
    for(GLsizei i = 0; i < count; i++) target_length += SRC_LEN(i);
    GLchar* target_string = malloc((target_length + 1) * sizeof(GLchar));
    size_t offset = 0;
    for(GLsizei i = 0; i < count; i++) {
        memcpy(&target_string[offset], string[i], SRC_LEN(i));
    }
    target_string[target_length] = 0;

#undef SRC_LEN
    // Lower samplerBuffer -> sampler2D when emulating texture buffers on ES 3.0
    // (see lower_sampler_buffers). If lowering applies, swap the source.
    if(current_context->emulate_texture_buffer) {
        GLchar* lowered = lower_sampler_buffers(target_string);
        if(lowered != NULL) {
            free(target_string);
            target_string = lowered;
        }
    }
    GLchar* new_source = optimize_shader(target_string, shader_info->shader_type, 460, current_context->shader_version);
    if(shader_info->source != NULL) free((void*)shader_info->source);
    if(!new_source) {
        printf("LTWShdrWp: failed to convert&optimize shader %u, skipping\n", shader);
        goto end;
    } else {
        //printf("\n\n\nShader Result\n%s\n\n\n", new_source);
        shader_info->source = new_source;
    }
    es3_functions.glShaderSource(shader, 1, &shader_info->source, 0);
    end:
    free(target_string);
}

// GLES 2.0/3.0 core shader & program functions that LTW previously left as
// empty STUBFUNCs. Same root cause as glGetFloatv / glGenQueries / glBufferData:
// ANGLE need not export core static entries through eglGetProcAddress, so MC
// got no-op stubs and shader compilation silently did nothing -> every
// pipeline failed to load. These thin forwarders call the host pointers
// already resolved into es3_functions at init. Android is unaffected (system
// ES statically exports them; LWJGL resolves via dlsym).

void glCompileShader(GLuint shader) {
    if(!current_context) return;
    if(es3_functions.glCompileShader) es3_functions.glCompileShader(shader);
}
void glDetachShader(GLuint program, GLuint shader) {
    if(!current_context) return;
    if(es3_functions.glDetachShader) es3_functions.glDetachShader(program, shader);
}
void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    if(!current_context) return;
    if(es3_functions.glBindAttribLocation) es3_functions.glBindAttribLocation(program, index, name);
}
GLint glGetAttribLocation(GLuint program, const GLchar* name) {
    if(!current_context) return -1;
    if(es3_functions.glGetAttribLocation) return es3_functions.glGetAttribLocation(program, name);
    return -1;
}
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    if(!current_context) return -1;
    if(es3_functions.glGetUniformLocation) return es3_functions.glGetUniformLocation(program, name);
    return -1;
}
void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    if(!current_context) return;
    if(es3_functions.glGetProgramiv) es3_functions.glGetProgramiv(program, pname, params);
}
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if(!current_context) return;
    if(es3_functions.glGetProgramInfoLog) es3_functions.glGetProgramInfoLog(program, bufSize, length, infoLog);
}
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    if(!current_context) return;
    if(es3_functions.glGetShaderInfoLog) es3_functions.glGetShaderInfoLog(shader, bufSize, length, infoLog);
}
void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source) {
    if(!current_context) return;
    if(es3_functions.glGetShaderSource) es3_functions.glGetShaderSource(shader, bufSize, length, source);
}
GLboolean glIsProgram(GLuint program) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsProgram) return es3_functions.glIsProgram(program);
    return GL_FALSE;
}
GLboolean glIsShader(GLuint shader) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsShader) return es3_functions.glIsShader(shader);
    return GL_FALSE;
}
void glValidateProgram(GLuint program) {
    if(!current_context) return;
    if(es3_functions.glValidateProgram) es3_functions.glValidateProgram(program);
}
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    if(!current_context) return;
    if(es3_functions.glGetActiveAttrib) es3_functions.glGetActiveAttrib(program, index, bufSize, length, size, type, name);
}
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    if(!current_context) return;
    if(es3_functions.glGetActiveUniform) es3_functions.glGetActiveUniform(program, index, bufSize, length, size, type, name);
}
void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders) {
    if(!current_context) return;
    if(es3_functions.glGetAttachedShaders) es3_functions.glGetAttachedShaders(program, maxCount, count, shaders);
}
void glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    if(!current_context) return;
    if(es3_functions.glGetUniformfv) es3_functions.glGetUniformfv(program, location, params);
}
void glGetUniformiv(GLuint program, GLint location, GLint* params) {
    if(!current_context) return;
    if(es3_functions.glGetUniformiv) es3_functions.glGetUniformiv(program, location, params);
}
void glDisableVertexAttribArray(GLuint index) {
    if(!current_context) return;
    if(es3_functions.glDisableVertexAttribArray) es3_functions.glDisableVertexAttribArray(index);
}
void glEnableVertexAttribArray(GLuint index) {
    if(!current_context) return;
    if(es3_functions.glEnableVertexAttribArray) es3_functions.glEnableVertexAttribArray(index);
}
void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* params) {
    if(!current_context) return;
    if(es3_functions.glGetVertexAttribfv) es3_functions.glGetVertexAttribfv(index, pname, params);
}
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint* params) {
    if(!current_context) return;
    if(es3_functions.glGetVertexAttribiv) es3_functions.glGetVertexAttribiv(index, pname, params);
}
void glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
    if(!current_context) return;
    if(es3_functions.glGetVertexAttribPointerv) es3_functions.glGetVertexAttribPointerv(index, pname, pointer);
}

/* GLES 2.0/3.0 core uniform / program / shader forwarders. Continued from
 * the glGetVertexAttribPointerv block above. Same STUBFUNC->forwarder fix:
 * ANGLE need not export core static entries via eglGetProcAddress, so LTW
 * previously returned no-op stubs for the glUniform, glUniformMatrix, program
 * binary and precision-format calls, silently breaking every MC shader
 * pipeline. */
void glUniform1f(GLint location, GLfloat v0) {
    if(!current_context) return;
    if(es3_functions.glUniform1f) es3_functions.glUniform1f(location, v0);
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniform1fv) es3_functions.glUniform1fv(location, count, value);
}

void glUniform1i(GLint location, GLint v0) {
    if(!current_context) return;
    if(es3_functions.glUniform1i) es3_functions.glUniform1i(location, v0);
}

void glUniform1iv(GLint location, GLsizei count, const GLint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform1iv) es3_functions.glUniform1iv(location, count, value);
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    if(!current_context) return;
    if(es3_functions.glUniform2f) es3_functions.glUniform2f(location, v0, v1);
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniform2fv) es3_functions.glUniform2fv(location, count, value);
}

void glUniform2i(GLint location, GLint v0, GLint v1) {
    if(!current_context) return;
    if(es3_functions.glUniform2i) es3_functions.glUniform2i(location, v0, v1);
}

void glUniform2iv(GLint location, GLsizei count, const GLint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform2iv) es3_functions.glUniform2iv(location, count, value);
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    if(!current_context) return;
    if(es3_functions.glUniform3f) es3_functions.glUniform3f(location, v0, v1, v2);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniform3fv) es3_functions.glUniform3fv(location, count, value);
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    if(!current_context) return;
    if(es3_functions.glUniform3i) es3_functions.glUniform3i(location, v0, v1, v2);
}

void glUniform3iv(GLint location, GLsizei count, const GLint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform3iv) es3_functions.glUniform3iv(location, count, value);
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    if(!current_context) return;
    if(es3_functions.glUniform4f) es3_functions.glUniform4f(location, v0, v1, v2, v3);
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniform4fv) es3_functions.glUniform4fv(location, count, value);
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    if(!current_context) return;
    if(es3_functions.glUniform4i) es3_functions.glUniform4i(location, v0, v1, v2, v3);
}

void glUniform4iv(GLint location, GLsizei count, const GLint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform4iv) es3_functions.glUniform4iv(location, count, value);
}

void glUniform1ui(GLint location, GLuint v0) {
    if(!current_context) return;
    if(es3_functions.glUniform1ui) es3_functions.glUniform1ui(location, v0);
}

void glUniform1uiv(GLint location, GLsizei count, const GLuint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform1uiv) es3_functions.glUniform1uiv(location, count, value);
}

void glUniform2ui(GLint location, GLuint v0, GLuint v1) {
    if(!current_context) return;
    if(es3_functions.glUniform2ui) es3_functions.glUniform2ui(location, v0, v1);
}

void glUniform2uiv(GLint location, GLsizei count, const GLuint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform2uiv) es3_functions.glUniform2uiv(location, count, value);
}

void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    if(!current_context) return;
    if(es3_functions.glUniform3ui) es3_functions.glUniform3ui(location, v0, v1, v2);
}

void glUniform3uiv(GLint location, GLsizei count, const GLuint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform3uiv) es3_functions.glUniform3uiv(location, count, value);
}

void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    if(!current_context) return;
    if(es3_functions.glUniform4ui) es3_functions.glUniform4ui(location, v0, v1, v2, v3);
}

void glUniform4uiv(GLint location, GLsizei count, const GLuint *value) {
    if(!current_context) return;
    if(es3_functions.glUniform4uiv) es3_functions.glUniform4uiv(location, count, value);
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix2fv) es3_functions.glUniformMatrix2fv(location, count, transpose, value);
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix3fv) es3_functions.glUniformMatrix3fv(location, count, transpose, value);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix4fv) es3_functions.glUniformMatrix4fv(location, count, transpose, value);
}

void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix2x3fv) es3_functions.glUniformMatrix2x3fv(location, count, transpose, value);
}

void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix3x2fv) es3_functions.glUniformMatrix3x2fv(location, count, transpose, value);
}

void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix2x4fv) es3_functions.glUniformMatrix2x4fv(location, count, transpose, value);
}

void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix4x2fv) es3_functions.glUniformMatrix4x2fv(location, count, transpose, value);
}

void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix3x4fv) es3_functions.glUniformMatrix3x4fv(location, count, transpose, value);
}

void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if(!current_context) return;
    if(es3_functions.glUniformMatrix4x3fv) es3_functions.glUniformMatrix4x3fv(location, count, transpose, value);
}

void glGetUniformuiv(GLuint program, GLint location, GLuint *params) {
    if(!current_context) return;
    if(es3_functions.glGetUniformuiv) es3_functions.glGetUniformuiv(program, location, params);
}

GLint glGetFragDataLocation(GLuint program, const GLchar *name) {
    if(!current_context) return -1;
    if(es3_functions.glGetFragDataLocation) return es3_functions.glGetFragDataLocation(program, name);
    return -1;
}

void glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const*uniformNames, GLuint *uniformIndices) {
    if(!current_context) return;
    if(es3_functions.glGetUniformIndices) es3_functions.glGetUniformIndices(program, uniformCount, uniformNames, uniformIndices);
}

void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params) {
    if(!current_context) return;
    if(es3_functions.glGetActiveUniformsiv) es3_functions.glGetActiveUniformsiv(program, uniformCount, uniformIndices, pname, params);
}

GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) {
    if(!current_context) return 0;
    if(es3_functions.glGetUniformBlockIndex) return es3_functions.glGetUniformBlockIndex(program, uniformBlockName);
    return 0;
}

void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params) {
    if(!current_context) return;
    if(es3_functions.glGetActiveUniformBlockiv) es3_functions.glGetActiveUniformBlockiv(program, uniformBlockIndex, pname, params);
}

void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName) {
    if(!current_context) return;
    if(es3_functions.glGetActiveUniformBlockName) es3_functions.glGetActiveUniformBlockName(program, uniformBlockIndex, bufSize, length, uniformBlockName);
}

void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {
    if(!current_context) return;
    if(es3_functions.glUniformBlockBinding) es3_functions.glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding);
}

void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) {
    if(!current_context) return;
    if(es3_functions.glGetShaderPrecisionFormat) es3_functions.glGetShaderPrecisionFormat(shadertype, precisiontype, range, precision);
}

void glReleaseShaderCompiler(void) {
    if(!current_context) return;
    if(es3_functions.glReleaseShaderCompiler) es3_functions.glReleaseShaderCompiler();
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat, const void *binary, GLsizei length) {
    if(!current_context) return;
    if(es3_functions.glShaderBinary) es3_functions.glShaderBinary(count, shaders, binaryformat, binary, length);
}

void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary) {
    if(!current_context) return;
    if(es3_functions.glGetProgramBinary) es3_functions.glGetProgramBinary(program, bufSize, length, binaryFormat, binary);
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length) {
    if(!current_context) return;
    if(es3_functions.glProgramBinary) es3_functions.glProgramBinary(program, binaryFormat, binary, length);
}

void glProgramParameteri(GLuint program, GLenum pname, GLint value) {
    if(!current_context) return;
    if(es3_functions.glProgramParameteri) es3_functions.glProgramParameteri(program, pname, value);
}

void glTransformFeedbackVaryings(GLuint program, GLsizei count, const GLchar *const*varyings, GLenum bufferMode) {
    if(!current_context) return;
    if(es3_functions.glTransformFeedbackVaryings) es3_functions.glTransformFeedbackVaryings(program, count, varyings, bufferMode);
}

void glGetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) {
    if(!current_context) return;
    if(es3_functions.glGetTransformFeedbackVarying) es3_functions.glGetTransformFeedbackVarying(program, index, bufSize, length, size, type, name);
}
