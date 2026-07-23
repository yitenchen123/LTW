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
#include <ctype.h>
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

/* --- samplerBuffer -> sampler2D source-level lowering ----------------------
 * ANGLE's Metal ES 3.0 backend does not implement GL_EXT_texture_buffer, so
 * its GLSL compiler rejects `samplerBuffer` ("Illegal use of reserved word")
 * and the `#extension GL_EXT_texture_buffer : enable` directive that
 * glsl_optimizer emits (ir_print_glsl_visitor.cpp). MobileGlues handles this
 * in its own conversion layer; LTW does the same here, lowering buffer
 * samplers to 2D samplers at the source level when the host has no
 * GL_EXT_texture_buffer. Pure text transformation (no function overloads) to
 * avoid MESA builtin signature conflicts.
 *
 *   #extension GL_EXT_texture_buffer : enable  -> (line dropped, blank kept)
 *   samplerBuffer / isamplerBuffer / usamplerBuffer -> sampler2D variants
 *   texelFetch(buf, x)   -> texelFetch(buf, ivec2(x, 0), 0)
 *   textureSize(buf)     -> textureSize(buf, 0).x
 * Only texelFetch/textureSize whose first argument is a declared buffer
 * sampler variable are rewritten, so ordinary 2D/3D calls are untouched.
 */
static int ltw_is_ident(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int ltw_contains(const char* s, size_t n, const char* needle) {
    size_t nl = strlen(needle);
    if(nl > n) return 0;
    for(size_t i = 0; i + nl <= n; i++)
        if(memcmp(s + i, needle, nl) == 0) return 1;
    return 0;
}

typedef struct { char** items; int count; int cap; } ltw_strlist;
static void ltw_sl_push(ltw_strlist* l, const char* s, size_t n) {
    if(l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = realloc(l->items, l->cap * sizeof(char*));
    }
    char* d = malloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    l->items[l->count++] = d;
}
static int ltw_sl_has(ltw_strlist* l, const char* s, size_t n) {
    for(int i = 0; i < l->count; i++)
        if(strlen(l->items[i]) == n && memcmp(l->items[i], s, n) == 0) return 1;
    return 0;
}
static void ltw_sl_free(ltw_strlist* l) {
    for(int i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

/* Collect names of variables declared as buffer samplers. */
static void ltw_collect_buf_samplers(const char* src, ltw_strlist* names) {
    static const char* types[] = {"usamplerBuffer", "isamplerBuffer", "samplerBuffer"};
    for(size_t t = 0; t < 3; t++) {
        const char* needle = types[t];
        size_t nlen = strlen(needle);
        const char* p = src;
        while((p = strstr(p, needle)) != NULL) {
            size_t pos = (size_t)(p - src);
            if(pos > 0 && ltw_is_ident((unsigned char)src[pos - 1])) { p += nlen; continue; }
            if(ltw_is_ident((unsigned char)src[pos + nlen])) { p += nlen; continue; }
            const char* q = p + nlen;
            while(*q && isspace((unsigned char)*q)) q++;
            const char* ns = q;
            while(ltw_is_ident((unsigned char)*q)) q++;
            size_t nl = (size_t)(q - ns);
            if(nl > 0 && !ltw_sl_has(names, ns, nl)) ltw_sl_push(names, ns, nl);
            p += nlen;
        }
    }
}

static char* ltw_lower_sampler_buffers(const char* src) {
    ltw_strlist names = {0};
    ltw_collect_buf_samplers(src, &names);

    size_t srclen = strlen(src);

    /* Fast path: nothing to lower. */
    if(names.count == 0 && !ltw_contains(src, srclen, "GL_EXT_texture_buffer")) {
        ltw_sl_free(&names);
        char* dup = malloc(srclen + 1);
        memcpy(dup, src, srclen + 1);
        return dup;
    }

    size_t cap = srclen + 64, len = 0;
    char* out = malloc(cap);
#define ENSURE(extra) do { if(len + (extra) + 1 > cap) { \
        while(len + (extra) + 1 > cap) cap *= 2; out = realloc(out, cap); } } while(0)

    size_t i = 0;
    while(i < srclen) {
        char c = src[i];

        /* Strip #extension GL_EXT_texture_buffer directive lines (kept as
         * blank lines to preserve line numbers for diagnostics). */
        if(c == '#') {
            size_t ls = i, j = i;
            while(j < srclen && src[j] != '\n') j++;
            if(ltw_contains(src + ls, j - ls, "GL_EXT_texture_buffer")) {
                i = j; /* drop line content; the trailing '\n' is emitted next */
                continue;
            }
            ENSURE(1); out[len++] = c; i++; continue;
        }

        /* Word-boundary guard for the keyword matches below. */
        if(i == 0 || !ltw_is_ident((unsigned char)src[i - 1])) {
            /* texelFetch(buf, x) -> texelFetch(buf, ivec2(x, 0), 0) */
            if(strncmp(src + i, "texelFetch", 10) == 0 && i + 10 <= srclen &&
               (i + 10 == srclen || !ltw_is_ident((unsigned char)src[i + 10]))) {
                size_t p = i + 10;
                while(p < srclen && src[p] != '(' && isspace((unsigned char)src[p])) p++;
                if(p < srclen && src[p] == '(') {
                    size_t open = p, close = (size_t)-1;
                    int depth = 0;
                    for(size_t d = open; d < srclen; d++) {
                        if(src[d] == '(') depth++;
                        else if(src[d] == ')') { depth--; if(depth == 0) { close = d; break; } }
                    }
                    if(close != (size_t)-1) {
                        size_t q = open + 1;
                        while(q < close && isspace((unsigned char)src[q])) q++;
                        size_t ns = q;
                        while(q < close && ltw_is_ident((unsigned char)src[q])) q++;
                        size_t nl = q - ns;
                        if(nl > 0 && ltw_sl_has(&names, src + ns, nl)) {
                            int d2 = 0; size_t comma = (size_t)-1;
                            for(size_t d = q; d < close; d++) {
                                if(src[d] == '(') d2++;
                                else if(src[d] == ')') d2--;
                                else if(src[d] == ',' && d2 == 0) { comma = d; break; }
                            }
                            if(comma != (size_t)-1) {
                                size_t s2 = comma + 1, e2 = close;
                                while(s2 < e2 && isspace((unsigned char)src[s2])) s2++;
                                while(e2 > s2 && isspace((unsigned char)src[e2 - 1])) e2--;
                                ENSURE(11 + nl + 8 + (e2 - s2) + 8);
                                memcpy(out + len, "texelFetch(", 11); len += 11;
                                memcpy(out + len, src + ns, nl); len += nl;
                                memcpy(out + len, ", ivec2(", 8); len += 8;
                                memcpy(out + len, src + s2, e2 - s2); len += (e2 - s2);
                                memcpy(out + len, ", 0), 0)", 8); len += 8;
                                i = close + 1;
                                continue;
                            }
                        }
                    }
                }
            }

            /* textureSize(buf) -> textureSize(buf, 0).x  (1-arg form only) */
            if(strncmp(src + i, "textureSize", 11) == 0 && i + 11 <= srclen &&
               (i + 11 == srclen || !ltw_is_ident((unsigned char)src[i + 11]))) {
                size_t p = i + 11;
                while(p < srclen && src[p] != '(' && isspace((unsigned char)src[p])) p++;
                if(p < srclen && src[p] == '(') {
                    size_t open = p, close = (size_t)-1;
                    int depth = 0;
                    for(size_t d = open; d < srclen; d++) {
                        if(src[d] == '(') depth++;
                        else if(src[d] == ')') { depth--; if(depth == 0) { close = d; break; } }
                    }
                    if(close != (size_t)-1) {
                        size_t q = open + 1;
                        while(q < close && isspace((unsigned char)src[q])) q++;
                        size_t ns = q;
                        while(q < close && ltw_is_ident((unsigned char)src[q])) q++;
                        size_t nl = q - ns;
                        if(nl > 0 && ltw_sl_has(&names, src + ns, nl)) {
                            int d2 = 0, has_comma = 0;
                            for(size_t d = q; d < close; d++) {
                                if(src[d] == '(') d2++;
                                else if(src[d] == ')') d2--;
                                else if(src[d] == ',' && d2 == 0) { has_comma = 1; break; }
                            }
                            if(!has_comma) {
                                ENSURE(12 + nl + 6);
                                memcpy(out + len, "textureSize(", 12); len += 12;
                                memcpy(out + len, src + ns, nl); len += nl;
                                memcpy(out + len, ", 0).x", 6); len += 6;
                                i = close + 1;
                                continue;
                            }
                        }
                    }
                }
            }

            /* type tokens -> 2D variants */
            const char* repl = NULL; size_t rlen = 0, mlen = 0;
            if(strncmp(src + i, "usamplerBuffer", 14) == 0 && i + 14 <= srclen &&
               (i + 14 == srclen || !ltw_is_ident((unsigned char)src[i + 14]))) {
                repl = "usampler2D"; rlen = 10; mlen = 14;
            } else if(strncmp(src + i, "isamplerBuffer", 14) == 0 && i + 14 <= srclen &&
                      (i + 14 == srclen || !ltw_is_ident((unsigned char)src[i + 14]))) {
                repl = "isampler2D"; rlen = 10; mlen = 14;
            } else if(strncmp(src + i, "samplerBuffer", 13) == 0 && i + 13 <= srclen &&
                      (i + 13 == srclen || !ltw_is_ident((unsigned char)src[i + 13]))) {
                repl = "sampler2D"; rlen = 9; mlen = 13;
            }
            if(repl) {
                ENSURE(rlen);
                memcpy(out + len, repl, rlen); len += rlen;
                i += mlen;
                continue;
            }
        }

        ENSURE(1);
        out[len++] = c;
        i++;
    }
#undef ENSURE
    out[len] = 0;
    ltw_sl_free(&names);
    return out;
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
    GLchar* new_source = optimize_shader(target_string, shader_info->shader_type, 460, current_context->shader_version);
    if(shader_info->source != NULL) free((void*)shader_info->source);
    if(!new_source) {
        printf("LTWShdrWp: failed to convert&optimize shader %u, skipping\n", shader);
        goto end;
    } else {
        /* When the host lacks GL_EXT_texture_buffer (e.g. ANGLE Metal ES 3.0),
         * lower samplerBuffer to sampler2D at the source level so the shader
         * still compiles. Mirrors MobileGlues' conversion-layer approach. */
        if(!current_context->buffer_texture_ext) {
            GLchar* lowered = ltw_lower_sampler_buffers(new_source);
            if(lowered) {
                free(new_source);
                new_source = lowered;
            }
        }
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
