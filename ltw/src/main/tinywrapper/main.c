/**
 * Created by: artDev, DuyKhanhTran
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include <stdio.h>
#include <dlfcn.h>

#include <stdbool.h>
#include <stdint.h>
#include "GL/gl.h"
#include <GLES3/gl3.h>
#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#include "proc.h"
#include "egl.h"
#include "glformats.h"
#include "main.h"
#include "swizzle.h"
#include "libraryinternal.h"
#include "env.h"

void glClearDepth(GLdouble depth) {
    if(!current_context) return;
    es3_functions.glClearDepthf((GLfloat) depth);
}

void *glMapBuffer(GLenum target, GLenum access) {
    if(!current_context) return NULL;

    GLenum access_range;
    GLint length;

    switch (target) {
        // GL 4.2
        case GL_ATOMIC_COUNTER_BUFFER:
        // GL 4.3
        case GL_DISPATCH_INDIRECT_BUFFER:
        case GL_SHADER_STORAGE_BUFFER:
        // GL 4.4
        case GL_QUERY_BUFFER:
            printf("ERROR: glMapBuffer unsupported target=0x%x\n", target);
            break; // not supported for now
	    case GL_DRAW_INDIRECT_BUFFER:
        case GL_TEXTURE_BUFFER:
            printf("ERROR: glMapBuffer unimplemented target=0x%x\n", target);
            break;
    }

    switch (access) {
        case GL_READ_ONLY:
            access_range = GL_MAP_READ_BIT;
            break;

        case GL_WRITE_ONLY:
            access_range = GL_MAP_WRITE_BIT;
            break;

        case GL_READ_WRITE:
            access_range = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
            break;
    }

    es3_functions.glGetBufferParameteriv(target, GL_BUFFER_SIZE, &length);
    return es3_functions.glMapBufferRange(target, 0, length, access_range);
}

INTERNAL int isProxyTexture(GLenum target) {
    switch (target) {
        case GL_PROXY_TEXTURE_1D:
        case GL_PROXY_TEXTURE_2D:
        case GL_PROXY_TEXTURE_3D:
        case GL_PROXY_TEXTURE_RECTANGLE_ARB:
            return 1;
    }
    return 0;
}

INTERNAL GLenum get_textarget_query_param(GLenum target) {
    switch (target) {
        case GL_TEXTURE_2D:
            return GL_TEXTURE_BINDING_2D;
        case GL_TEXTURE_2D_MULTISAMPLE:
            return GL_TEXTURE_BINDING_2D_MULTISAMPLE;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
            return GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY;
        case GL_TEXTURE_3D:
            return GL_TEXTURE_BINDING_3D;
        case GL_TEXTURE_2D_ARRAY:
            return GL_TEXTURE_BINDING_2D_ARRAY;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
        case GL_TEXTURE_CUBE_MAP:
            return GL_TEXTURE_BINDING_CUBE_MAP;
        case GL_TEXTURE_CUBE_MAP_ARRAY:
            return GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;
        case GL_TEXTURE_BUFFER:
            return GL_TEXTURE_BUFFER_BINDING;
        default:
            return 0;
    }
}

static int inline nlevel(int size, int level) {
    if(size) {
        size>>=level;
        if(!size) size=1;
    }
    return size;
}

static bool trigger_texlevelparameter = false;

static bool check_texlevelparameter() {
    if(current_context->es31) return true;
    if(trigger_texlevelparameter) return false;
    printf("glGetTexLevelParameter* functions are not supported below OpenGL ES 3.1\n");
    trigger_texlevelparameter = true;
    return false;
}

static void proxy_getlevelparameter(GLenum target, GLint level, GLenum pname, GLint *params) {
    switch (pname) {
        case GL_TEXTURE_WIDTH:
            (*params) = nlevel(current_context->proxy_width, level);
            break;
        case GL_TEXTURE_HEIGHT:
            (*params) = nlevel(current_context->proxy_height, level);
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            (*params) = current_context->proxy_intformat;
            break;
    }
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
    if(!current_context) return;
    if(isProxyTexture(target)) {
        GLint param = 0;
        proxy_getlevelparameter(target, level, pname, &param);
        *params = (GLfloat) param;
        return;
    }
    if(!check_texlevelparameter()) return;
    es3_functions.glGetTexLevelParameterfv(target, level, pname, params);
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
    if(!current_context) return;
    if (isProxyTexture(target)) {
        proxy_getlevelparameter(target, level, pname, params);
        return;
    }
    if(!check_texlevelparameter()) return;
    es3_functions.glGetTexLevelParameteriv(target, level, pname, params);

}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) {
    if(!current_context) return;
    if (isProxyTexture(target)) {
        current_context->proxy_width = ((width<<level)>current_context->maxTextureSize)?0:width;
        current_context->proxy_height = ((height<<level)>current_context->maxTextureSize)?0:height;
        current_context->proxy_intformat = internalformat;
    } else {
        if(data != NULL) swizzle_process_upload(target, &format, &type);
        pick_internalformat(&internalformat, &type, &format, &data);
        es3_functions.glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
    }
}

INTERNAL bool filter_params_integer(GLenum target, GLenum pname, GLint param) {
    return true;
}
INTERNAL bool filter_params_float(GLenum target, GLenum pname, GLfloat param) {
    if(pname == GL_TEXTURE_LOD_BIAS) {
        if(param != 0.0f) {
            static bool lodbias_trigger = false;
            if(!lodbias_trigger) {
                printf("LTW: setting GL_TEXTURE_LOD_BIAS to nondefault value not supported\n");
            }
        }
        return false;
    }
    return true;
}
void glTexParameterf( 	GLenum target,
                         GLenum pname,
                         GLfloat param) {
    if(!current_context) return;
    if(!filter_params_integer(target, pname, (GLint) param)) return;
    if(!filter_params_float(target, pname, param)) return;
    es3_functions.glTexParameterf(target, pname, param);
}
void glTexParameteri( 	GLenum target,
                         GLenum pname,
                         GLint param) {
    if(!current_context) return;
    if(!filter_params_integer(target, pname, param)) return;
    if(!filter_params_float(target, pname, (GLfloat)param)) return;
    swizzle_process_swizzle_param(target, pname, &param);
    es3_functions.glTexParameteri(target, pname, param);
}

void glTexParameterfv( 	GLenum target,
                          GLenum pname,
                          const GLfloat * params) {
    if(!current_context) return;
    if(!filter_params_integer(target, pname, (GLint)*params)) return;
    if(!filter_params_float(target, pname, *params)) return;
    es3_functions.glTexParameterfv(target, pname, params);
}
void glTexParameteriv( 	GLenum target,
                          GLenum pname,
                          const GLint * params) {
    if(!current_context) return;
    if(!filter_params_integer(target, pname, *params)) return;
    if(!filter_params_float(target, pname, (GLfloat)*params)) return;
    swizzle_process_swizzle_param(target, pname, params);
    es3_functions.glTexParameteriv(target, pname, params);
}
static bool trigger_gltexparameteri = false;
void glTexParameterIiv( 	GLenum target,
                           GLenum pname,
                           const GLint * params) {
    if(!current_context) return;
    if(pname != GL_TEXTURE_SWIZZLE_RGBA) {
        if(!trigger_gltexparameteri) {
            printf("LTW: glTexParameterIiv for parameters other than GL_TEXTURE_SWIZZLE_RGBA is not supported\n");
            trigger_gltexparameteri = true;
        }
        return;
    }
    swizzle_process_swizzle_param(target, pname, params);
}

void glTexParameterIuiv( 	GLenum target,
                            GLenum pname,
                            const GLuint * params) {
    if(!current_context) return;
    if(pname != GL_TEXTURE_SWIZZLE_RGBA) {
        if(!trigger_gltexparameteri) {
            printf("LTW: glTexParameterIuiv for parameters other than GL_TEXTURE_SWIZZLE_RGBA is not supported\n");
            trigger_gltexparameteri = true;
        }
        return;
    }
    swizzle_process_swizzle_param(target, pname, params);
}

void glRenderbufferStorage(	GLenum target,
                               GLenum internalformat,
                               GLsizei width,
                               GLsizei height) {
    if(!current_context) return;
    if(internalformat == GL_DEPTH_COMPONENT) internalformat = GL_DEPTH_COMPONENT16;
    es3_functions.glRenderbufferStorage(target, internalformat, width, height);
}

static bool never_flush_buffers;
static bool coherent_dynamic_storage;

void glBufferStorage(GLenum target,
                     GLsizeiptr size,
                     const void * data,
                     GLbitfield flags) {
    if(!current_context || !current_context->buffer_storage) return;
    // Enable coherence to make sure the buffers are synced without flushing.
    if(never_flush_buffers && ((flags & GL_MAP_PERSISTENT_BIT) != 0)) {
        flags |= GL_MAP_COHERENT_BIT;
    }
    // Force dynamic storage buffers to be coherent (for working around driver bugs)
    if(coherent_dynamic_storage && (flags & GL_DYNAMIC_STORAGE_BIT) != 0) {
        flags |= (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    }
    es3_functions.glBufferStorageEXT(target, size, data, flags);
}

void *glMapBufferRange( 	GLenum target,
                           GLintptr offset,
                           GLsizeiptr length,
                           GLbitfield access) {
    if(!current_context) return NULL;
    if(never_flush_buffers) access &= ~GL_MAP_FLUSH_EXPLICIT_BIT;
    return es3_functions.glMapBufferRange(target, offset, length, access);
}

void glFlushMappedBufferRange( 	GLenum target,
                                  GLintptr offset,
                                  GLsizeiptr length) {
    if(!never_flush_buffers) es3_functions.glFlushMappedBufferRange(target, offset, length);
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    if(!current_context || name != GL_EXTENSIONS) return NULL;
    if(index >= current_context->nextras) {
        return es3_functions.glGetStringi(name, index - current_context->nextras);
    } else {
        return (const GLubyte*)current_context->extra_extensions_array[index];
    }
}

const GLubyte* glGetString(GLenum name) {
    if(!current_context) return NULL;
    switch(name) {
        case GL_VERSION:
            return (const GLubyte*)"3.3 OpenLTW (Built on: "__DATE__"/"__TIME__")";
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte*)"4.60 LTW";
        case GL_VENDOR:
            return (const GLubyte*)"artDev, SerpentSpirale, CADIndie";
        case GL_EXTENSIONS:
            if(current_context->extensions_string != NULL) return (const GLubyte*)current_context->extensions_string;
            return (const GLubyte*)es3_functions.glGetString(GL_EXTENSIONS);
        default:
            return es3_functions.glGetString(name);
    }
}

static bool debug = false;

void glEnable(GLenum cap) {
    if(!current_context) return;
    if(cap == GL_DEBUG_OUTPUT && !debug) return;
    es3_functions.glEnable(cap);
}

INTERNAL int get_buffer_index(GLenum buffer) {
    switch (buffer) {
        case GL_ARRAY_BUFFER: return 0;
        case GL_COPY_READ_BUFFER: return 1;
        case GL_COPY_WRITE_BUFFER: return 2;
        case GL_PIXEL_PACK_BUFFER: return 3;
        case GL_PIXEL_UNPACK_BUFFER: return 4;
        case GL_TRANSFORM_FEEDBACK_BUFFER: return 5;
        case GL_UNIFORM_BUFFER: return 6;
        case GL_SHADER_STORAGE_BUFFER: return 7;
        case GL_DRAW_INDIRECT_BUFFER: return 8;
        default: return -1;
    }
}

INTERNAL int get_base_buffer_index(GLenum buffer) {
    switch (buffer) {
        case GL_ATOMIC_COUNTER_BUFFER: return 0;
        case GL_SHADER_STORAGE_BUFFER: return 1;
        case GL_TRANSFORM_FEEDBACK_BUFFER: return 2;
        case GL_UNIFORM_BUFFER: return 3;
        default: return -1;
    }
}

INTERNAL GLenum get_base_buffer_enum(int buffer_index) {
    switch (buffer_index) {
        case 0: return GL_ATOMIC_COUNTER_BUFFER;
        case 1: return GL_SHADER_STORAGE_BUFFER;
        case 2: return GL_TRANSFORM_FEEDBACK_BUFFER;
        case 3: return GL_UNIFORM_BUFFER;
        default: return -1;
    }
}

// GLES 3.0 core buffer functions. Forward to the host pointer resolved at
// init. Without these wrappers LTW would expose empty STUBFUNCs via
// eglGetProcAddress (same root cause as glGetFloatv / glGenQueries): ANGLE
// need not export core static entries through the proc-address API. The
// empty stubs were silently no-op'ing glBufferData/glUnmapBuffer, and
// glMapBuffer's glGetBufferParameteriv length query returned garbage, so
// Sodium's GlBuffer$Direct.map() got a NULL/invalid pointer and crashed.
void glGenBuffers(GLsizei n, GLuint* buffers) {
    if(!current_context) return;
    if(es3_functions.glGenBuffers) es3_functions.glGenBuffers(n, buffers);
}
void glDeleteBuffers(GLsizei n, const GLuint* buffers) {
    if(!current_context) return;
    if(es3_functions.glDeleteBuffers) es3_functions.glDeleteBuffers(n, buffers);
}
GLboolean glIsBuffer(GLuint buffer) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsBuffer) return es3_functions.glIsBuffer(buffer);
    return GL_FALSE;
}
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if(!current_context) return;
    if(es3_functions.glBufferData) es3_functions.glBufferData(target, size, data, usage);
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if(!current_context) return;
    if(es3_functions.glBufferSubData) es3_functions.glBufferSubData(target, offset, size, data);
}
GLboolean glUnmapBuffer(GLenum target) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glUnmapBuffer) return es3_functions.glUnmapBuffer(target);
    return GL_FALSE;
}
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    if(!current_context) return;
    if(es3_functions.glGetBufferParameteriv) es3_functions.glGetBufferParameteriv(target, pname, params);
}
void glGetBufferPointerv(GLenum target, GLenum pname, void** params) {
    if(!current_context) return;
    if(es3_functions.glGetBufferPointerv) es3_functions.glGetBufferPointerv(target, pname, params);
}

void glBindBuffer(GLenum buffer, GLuint name) {
    if(!current_context) return;
    es3_functions.glBindBuffer(buffer, name);
    int buffer_index = get_buffer_index(buffer);
    if(buffer_index == -1) return;
    current_context->bound_buffers[buffer_index] = name;
}

static basebuffer_binding_t* set_basebuffer(GLenum target, GLuint index, GLuint buffer) {
    int buffer_mapindex = get_base_buffer_index(target);
    if(buffer_mapindex == -1) return NULL;
    if(!buffer) {
        basebuffer_binding_t *old_binding = unordered_map_remove(current_context->bound_basebuffers[buffer_mapindex], (void*)index);
        free(old_binding);
        return NULL;
    }else {
        basebuffer_binding_t *binding = unordered_map_get(current_context->bound_basebuffers[buffer_mapindex], (void*)index);
        if(binding == NULL) {
            binding = calloc(1, sizeof(basebuffer_binding_t));
            unordered_map_put(current_context->bound_basebuffers[buffer_mapindex], (void*)index, binding);
        }
        binding->index = index;
        binding->buffer = buffer;
        return binding;
    }
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    if(!current_context) return;
    es3_functions.glBindBufferBase(target, index, buffer);
    basebuffer_binding_t * binding = set_basebuffer(target, index, buffer);
    if(!binding) return;
    binding->ranged = false;
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    if(!current_context) return;
    es3_functions.glBindBufferRange(target, index, buffer, offset, size);
    basebuffer_binding_t * binding = set_basebuffer(target, index, buffer);
    if(!binding) return;
    binding->ranged = true;
    binding->size = size;
    binding->offset = offset;
}

void glUseProgram(GLuint program) {
    if(!current_context) return;
    es3_functions.glUseProgram(program);
    current_context->program = program;
}

void glGetIntegerv(GLenum pname, GLint* data) {
    if(!current_context) return;
    switch (pname) {
        case GL_MAJOR_VERSION:
            *data = 3;
            return;
        case GL_MINOR_VERSION:
            *data = 3;
            return;
        case GL_NUM_EXTENSIONS:
            es3_functions.glGetIntegerv(pname, data);
            (*data) += current_context->nextras;
            printf("GL_NUM_EXTENSIONS: %i\n", (*data));
            return;
        case GL_MAX_COLOR_ATTACHMENTS:
            *data = MAX_FBTARGETS;
            return;
        case GL_MAX_DRAW_BUFFERS:
            *data = current_context->max_drawbuffers;
            return;
        case GL_CONTEXT_FLAGS:
            // You literally just can't enable robustness or debugging in GLFW
            *data = GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT;
            return;
		case GL_CONTEXT_PROFILE_MASK:
			// LTW is always core profile
			*data = GL_CONTEXT_CORE_PROFILE_BIT;
			return;
        default:
            es3_functions.glGetIntegerv(pname, data);
    }
}

void glGetFloatv(GLenum pname, GLfloat* data) {
    if(!current_context) return;
    if(es3_functions.glGetFloatv != NULL) {
        es3_functions.glGetFloatv(pname, data);
        return;
    }
    /* GLES implementations are not required to expose glGetFloatv through
     * eglGetProcAddress (it's a core static entry in the ES spec, not an
     * extension function), so some ANGLE builds return NULL for it -- in which
     * case LTW would otherwise hand MC an empty stub and queries like
     * GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT come back as 0, crashing the sampler
     * validation ("maxAnisotropy ... must be >= 1 and <= 0"). Fall back to
     * glGetIntegerv and widen to float; the ES spec guarantees both return the
     * same logical values for the same pname. 16 slots covers every standard
     * scalar/vector state query MC/Sodium issues. */
    GLint idata[16] = {0};
    es3_functions.glGetIntegerv(pname, idata);
    if(data) {
        for(int i = 0; i < 16; i++) data[i] = (GLfloat)idata[i];
    }
}

void glDepthRange(GLdouble nearVal,
                  GLdouble farVal) {
    if(!current_context) return;
    es3_functions.glDepthRangef((GLfloat)nearVal, (GLfloat)farVal);
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    if(!current_context) return;
    es3_functions.glDeleteTextures(n, textures);
    for(int i = 0; i < n; i++) {
        void* tracker = unordered_map_remove(current_context->texture_swztrack_map, (void*)textures[i]);
        free(tracker);
        // Release the internal 2D texture backing a deleted buffer texture.
        if(current_context->emulate_texture_buffer) {
            void* e = unordered_map_remove(current_context->texbuf_emul_map,
                                           (void*)(uintptr_t)textures[i]);
            if(e) {
                GLuint internal2D = (GLuint)(uintptr_t)e;
                es3_functions.glDeleteTextures(1, &internal2D);
                if(current_context->bound_buf_texture == textures[i])
                    current_context->bound_buf_texture = 0;
            }
        }
    }
}

static bool buf_tex_trigger = false;

// ---- texture-buffer runtime emulation (ES 3.0) ----------------------------
// When the host lacks GL_EXT_texture_buffer, LTW services GL_TEXTURE_BUFFER
// by uploading the referenced buffer object's store into an internal 2D
// texture (height 1) and remapping the GL_TEXTURE_BUFFER bind target to
// GL_TEXTURE_2D. The shader side is lowered to sampler2D (see
// shader_wrapper.c), so MC's isamplerBuffer + texelFetch reads land on the
// 2D texture. Only active when current_context->emulate_texture_buffer.

// Map a buffer-texture sized internal format to (pixel format, pixel type,
// element size in bytes). Returns false for unsupported formats.
static bool emul_map_format(GLenum internalFormat, GLenum* fmt, GLenum* type, GLint* elemsize) {
    switch(internalFormat) {
        case GL_R8I:        *fmt = GL_RED_INTEGER; *type = GL_BYTE;           *elemsize = 1; return true;
        case GL_R8UI:       *fmt = GL_RED_INTEGER; *type = GL_UNSIGNED_BYTE;  *elemsize = 1; return true;
        case GL_R16I:       *fmt = GL_RED_INTEGER; *type = GL_SHORT;          *elemsize = 2; return true;
        case GL_R16UI:      *fmt = GL_RED_INTEGER; *type = GL_UNSIGNED_SHORT; *elemsize = 2; return true;
        case GL_R32I:       *fmt = GL_RED_INTEGER; *type = GL_INT;            *elemsize = 4; return true;
        case GL_R32UI:      *fmt = GL_RED_INTEGER; *type = GL_UNSIGNED_INT;   *elemsize = 4; return true;
        case GL_R32F:       *fmt = GL_RED;         *type = GL_FLOAT;          *elemsize = 4; return true;
        case GL_R8:         *fmt = GL_RED;         *type = GL_UNSIGNED_BYTE;  *elemsize = 1; return true;
        default:
            printf("LTW texbuf emul: unsupported internalFormat 0x%x\n", internalFormat);
            return false;
    }
}

// Look up or create the internal GL_TEXTURE_2D name backing a buffer texture.
static GLuint emul_get_2d_for_buftex(GLuint bufTexName) {
    void* existing = unordered_map_get(current_context->texbuf_emul_map,
                                       (void*)(uintptr_t)bufTexName);
    if(existing) return (GLuint)(uintptr_t)existing;
    GLuint internal2D = 0;
    es3_functions.glGenTextures(1, &internal2D);
    unordered_map_put(current_context->texbuf_emul_map,
                      (void*)(uintptr_t)bufTexName, (void*)(uintptr_t)internal2D);
    return internal2D;
}

// Read `buffer`'s data store and upload it as a 1xN 2D texture into
// `internal2D`. Saves/restores GL_ARRAY_BUFFER and GL_TEXTURE_2D bindings.
static void emul_upload_buffer(GLuint internal2D, GLenum internalFormat,
                               GLuint buffer, GLintptr offset, GLsizeiptr size,
                               bool ranged) {
    GLenum fmt, type; GLint elemsize;
    if(!emul_map_format(internalFormat, &fmt, &type, &elemsize)) return;

    GLint prevAB = 0, prev2D = 0;
    es3_functions.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevAB);
    es3_functions.glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev2D);

    es3_functions.glBindBuffer(GL_ARRAY_BUFFER, buffer);
    GLint bufSize = 0;
    es3_functions.glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufSize);
    if(!ranged) { offset = 0; size = bufSize; }
    if(size <= 0 || offset < 0 || offset + size > bufSize) {
        es3_functions.glBindBuffer(GL_ARRAY_BUFFER, prevAB);
        return;
    }
    void* data = es3_functions.glMapBufferRange(GL_ARRAY_BUFFER, offset, size, GL_MAP_READ_BIT);
    if(data) {
        es3_functions.glBindTexture(GL_TEXTURE_2D, internal2D);
        GLint width = (GLint)(size / elemsize);
        es3_functions.glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, 1, 0, fmt, type, data);
        es3_functions.glUnmapBuffer(GL_ARRAY_BUFFER);
    } else {
        printf("LTW texbuf emul: failed to map buffer %u\n", buffer);
    }
    es3_functions.glBindTexture(GL_TEXTURE_2D, prev2D);
    es3_functions.glBindBuffer(GL_ARRAY_BUFFER, prevAB);
}

void glTexBuffer(GLenum target, GLenum internalFormat, GLuint buffer) {
    if(!current_context) return;
    if(current_context->emulate_texture_buffer) {
        if(target != GL_TEXTURE_BUFFER) return;
        GLuint bufTex = current_context->bound_buf_texture;
        if(bufTex == 0) return;
        GLuint internal2D = emul_get_2d_for_buftex(bufTex);
        emul_upload_buffer(internal2D, internalFormat, buffer, 0, 0, false);
        return;
    }
    if(current_context->es32) es3_functions.glTexBuffer(target, internalFormat, buffer);
    else if(current_context->buffer_texture_ext) es3_functions.glTexBufferEXT(target, internalFormat, buffer);
    else if(!buf_tex_trigger) {
        buf_tex_trigger = true;
        printf("LTW: Buffer textures aren't supported on your device\n");
    }
}

void glTexBufferARB(GLenum target, GLenum internalFormat, GLuint buffer) {
    glTexBuffer(target, internalFormat, buffer);
}

void glTexBufferRange(GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    if(!current_context) return;
    if(current_context->emulate_texture_buffer) {
        if(target != GL_TEXTURE_BUFFER) return;
        GLuint bufTex = current_context->bound_buf_texture;
        if(bufTex == 0) return;
        GLuint internal2D = emul_get_2d_for_buftex(bufTex);
        emul_upload_buffer(internal2D, internalFormat, buffer, offset, size, true);
        return;
    }
    if(current_context->es32) es3_functions.glTexBufferRange(target, internalFormat, buffer, offset, size);
    else if(current_context->buffer_texture_ext) es3_functions.glTexBufferRangeEXT(target, internalFormat, buffer, offset, size);
    else if(!buf_tex_trigger) {
        buf_tex_trigger = true;
        printf("LTW: Buffer textures aren't supported on your device\n");
    }
}

void glTexBufferRangeARB(GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    glTexBufferRange(target, internalFormat, buffer, offset, size);
}

static bool noerror;

__attribute((constructor)) void init_noerror() {
    noerror = env_istrue("LIBGL_NOERROR");
    debug = env_istrue("LTW_DEBUG");
    never_flush_buffers = env_istrue_d("LTW_NEVER_FLUSH_BUFFERS", true);
    coherent_dynamic_storage = env_istrue_d("LTW_COHERENT_DYNAMIC_STORAGE", true);
    if(!noerror) printf("LTW will NOT ignore GL errors. This may break mods, consider yourself warned.\n");
    if(coherent_dynamic_storage) printf("LTW will force dynamic storage buffers to be coherent.\n");
    if(debug) printf("LTW will allow GL_DEBUG_OUTPUT to be enabled. Expect massive logs.\n");
    if(never_flush_buffers) printf("LTW will prevent all explicit buffer flushes.\n");
}

GLenum glGetError() {
    if(noerror) return 0;
    else return es3_functions.glGetError();
}

void glDebugMessageControl( 	GLenum source,
                               GLenum type,
                               GLenum severity,
                               GLsizei count,
                               const GLuint *ids,
                               GLboolean enabled) {
    //STUB
}

/* GLES 2.0/3.0 core state / texture / sampler / draw / getter forwarders.
 * Same STUBFUNC->forwarder fix as the buffer/shader/vertexattrib wrappers:
 * ANGLE need not export core static entries via eglGetProcAddress, so LTW
 * previously returned no-op stubs (glClear/glViewport/glEnable/glDrawArrays
 * etc. silently did nothing). Forward to es3_functions host pointers. */
void glActiveTexture(GLenum texture) {
    if(!current_context) return;
    if(es3_functions.glActiveTexture) es3_functions.glActiveTexture(texture);
}

void glBindSampler(GLuint unit, GLuint sampler) {
    if(!current_context) return;
    if(es3_functions.glBindSampler) es3_functions.glBindSampler(unit, sampler);
}

void glBindTexture(GLenum target, GLuint texture) {
    if(!current_context) return;
    // Emulate GL_TEXTURE_BUFFER by remapping the bind to an internal 2D
    // texture (one per buffer-texture name). ES 3.0 has no GL_TEXTURE_BUFFER
    // target, so passing it through to the host would just generate an error.
    if(current_context->emulate_texture_buffer && target == GL_TEXTURE_BUFFER) {
        current_context->bound_buf_texture = texture;
        GLuint internal2D = 0;
        if(texture != 0) internal2D = emul_get_2d_for_buftex(texture);
        if(es3_functions.glBindTexture) es3_functions.glBindTexture(GL_TEXTURE_2D, internal2D);
        return;
    }
    if(es3_functions.glBindTexture) es3_functions.glBindTexture(target, texture);
}

void glBindVertexArray(GLuint array) {
    if(!current_context) return;
    if(es3_functions.glBindVertexArray) es3_functions.glBindVertexArray(array);
}

void glClear(GLbitfield mask) {
    if(!current_context) return;
    if(es3_functions.glClear) es3_functions.glClear(mask);
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if(!current_context) return;
    if(es3_functions.glClearColor) es3_functions.glClearColor(red, green, blue, alpha);
}

void glClearDepthf(GLfloat d) {
    if(!current_context) return;
    if(es3_functions.glClearDepthf) es3_functions.glClearDepthf(d);
}

void glClearStencil(GLint s) {
    if(!current_context) return;
    if(es3_functions.glClearStencil) es3_functions.glClearStencil(s);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    if(!current_context) return;
    if(es3_functions.glColorMask) es3_functions.glColorMask(red, green, blue, alpha);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
    if(!current_context) return;
    if(es3_functions.glCompressedTexImage2D) es3_functions.glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data) {
    if(!current_context) return;
    if(es3_functions.glCompressedTexImage3D) es3_functions.glCompressedTexImage3D(target, level, internalformat, width, height, depth, border, imageSize, data);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {
    if(!current_context) return;
    if(es3_functions.glCompressedTexSubImage2D) es3_functions.glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}

void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data) {
    if(!current_context) return;
    if(es3_functions.glCompressedTexSubImage3D) es3_functions.glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    if(!current_context) return;
    if(es3_functions.glCopyBufferSubData) es3_functions.glCopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset, size);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    if(!current_context) return;
    if(es3_functions.glCopyTexImage2D) es3_functions.glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
}

void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    if(!current_context) return;
    if(es3_functions.glCopyTexSubImage3D) es3_functions.glCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y, width, height);
}

void glCullFace(GLenum mode) {
    if(!current_context) return;
    if(es3_functions.glCullFace) es3_functions.glCullFace(mode);
}

void glDeleteSamplers(GLsizei count, const GLuint *samplers) {
    if(!current_context) return;
    if(es3_functions.glDeleteSamplers) es3_functions.glDeleteSamplers(count, samplers);
}

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    if(!current_context) return;
    if(es3_functions.glDeleteVertexArrays) es3_functions.glDeleteVertexArrays(n, arrays);
}

void glDepthFunc(GLenum func) {
    if(!current_context) return;
    if(es3_functions.glDepthFunc) es3_functions.glDepthFunc(func);
}

void glDepthMask(GLboolean flag) {
    if(!current_context) return;
    if(es3_functions.glDepthMask) es3_functions.glDepthMask(flag);
}

void glDepthRangef(GLfloat n, GLfloat f) {
    if(!current_context) return;
    if(es3_functions.glDepthRangef) es3_functions.glDepthRangef(n, f);
}

void glDisable(GLenum cap) {
    if(!current_context) return;
    if(es3_functions.glDisable) es3_functions.glDisable(cap);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if(!current_context) return;
    if(es3_functions.glDrawArrays) es3_functions.glDrawArrays(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if(!current_context) return;
    if(es3_functions.glDrawElements) es3_functions.glDrawElements(mode, count, type, indices);
}

void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) {
    if(!current_context) return;
    if(es3_functions.glDrawElementsInstanced) es3_functions.glDrawElementsInstanced(mode, count, type, indices, instancecount);
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) {
    if(!current_context) return;
    if(es3_functions.glDrawRangeElements) es3_functions.glDrawRangeElements(mode, start, end, count, type, indices);
}

void glFinish(void) {
    if(!current_context) return;
    if(es3_functions.glFinish) es3_functions.glFinish();
}

void glFlush(void) {
    if(!current_context) return;
    if(es3_functions.glFlush) es3_functions.glFlush();
}

void glFrontFace(GLenum mode) {
    if(!current_context) return;
    if(es3_functions.glFrontFace) es3_functions.glFrontFace(mode);
}

void glGenSamplers(GLsizei count, GLuint *samplers) {
    if(!current_context) return;
    if(es3_functions.glGenSamplers) es3_functions.glGenSamplers(count, samplers);
}

void glGenTextures(GLsizei n, GLuint *textures) {
    if(!current_context) return;
    if(es3_functions.glGenTextures) es3_functions.glGenTextures(n, textures);
}

void glGenVertexArrays(GLsizei n, GLuint *arrays) {
    if(!current_context) return;
    if(es3_functions.glGenVertexArrays) es3_functions.glGenVertexArrays(n, arrays);
}

void glGenerateMipmap(GLenum target) {
    if(!current_context) return;
    if(es3_functions.glGenerateMipmap) es3_functions.glGenerateMipmap(target);
}

void glGetBooleanv(GLenum pname, GLboolean *data) {
    if(!current_context) return;
    if(es3_functions.glGetBooleanv) es3_functions.glGetBooleanv(pname, data);
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
    if(!current_context) return;
    if(es3_functions.glGetBufferParameteri64v) es3_functions.glGetBufferParameteri64v(target, pname, params);
}

void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) {
    if(!current_context) return;
    if(es3_functions.glGetInteger64i_v) es3_functions.glGetInteger64i_v(target, index, data);
}

void glGetInteger64v(GLenum pname, GLint64 *data) {
    if(!current_context) return;
    if(es3_functions.glGetInteger64v) es3_functions.glGetInteger64v(pname, data);
}

void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
    if(!current_context) return;
    if(es3_functions.glGetIntegeri_v) es3_functions.glGetIntegeri_v(target, index, data);
}

void glGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params) {
    if(!current_context) return;
    if(es3_functions.glGetInternalformativ) es3_functions.glGetInternalformativ(target, internalformat, pname, bufSize, params);
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) {
    if(!current_context) return;
    if(es3_functions.glGetSamplerParameterfv) es3_functions.glGetSamplerParameterfv(sampler, pname, params);
}

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) {
    if(!current_context) return;
    if(es3_functions.glGetSamplerParameteriv) es3_functions.glGetSamplerParameteriv(sampler, pname, params);
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    if(!current_context) return;
    if(es3_functions.glGetTexParameterfv) es3_functions.glGetTexParameterfv(target, pname, params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    if(!current_context) return;
    if(es3_functions.glGetTexParameteriv) es3_functions.glGetTexParameteriv(target, pname, params);
}

void glHint(GLenum target, GLenum mode) {
    if(!current_context) return;
    if(es3_functions.glHint) es3_functions.glHint(target, mode);
}

GLboolean glIsEnabled(GLenum cap) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsEnabled) return es3_functions.glIsEnabled(cap);
    return GL_FALSE;
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsFramebuffer) return es3_functions.glIsFramebuffer(framebuffer);
    return GL_FALSE;
}

GLboolean glIsSampler(GLuint sampler) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsSampler) return es3_functions.glIsSampler(sampler);
    return GL_FALSE;
}

GLboolean glIsTexture(GLuint texture) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsTexture) return es3_functions.glIsTexture(texture);
    return GL_FALSE;
}

GLboolean glIsVertexArray(GLuint array) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsVertexArray) return es3_functions.glIsVertexArray(array);
    return GL_FALSE;
}

void glLineWidth(GLfloat width) {
    if(!current_context) return;
    if(es3_functions.glLineWidth) es3_functions.glLineWidth(width);
}

void glPixelStorei(GLenum pname, GLint param) {
    if(!current_context) return;
    if(es3_functions.glPixelStorei) es3_functions.glPixelStorei(pname, param);
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    if(!current_context) return;
    if(es3_functions.glPolygonOffset) es3_functions.glPolygonOffset(factor, units);
}

void glSampleCoverage(GLclampf value, GLboolean invert) {
    if(!current_context) return;
    if(es3_functions.glSampleCoverage) es3_functions.glSampleCoverage(value, invert);
}

void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    if(!current_context) return;
    if(es3_functions.glSamplerParameterf) es3_functions.glSamplerParameterf(sampler, pname, param);
}

void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) {
    if(!current_context) return;
    if(es3_functions.glSamplerParameterfv) es3_functions.glSamplerParameterfv(sampler, pname, param);
}

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    if(!current_context) return;
    if(es3_functions.glSamplerParameteri) es3_functions.glSamplerParameteri(sampler, pname, param);
}

void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) {
    if(!current_context) return;
    if(es3_functions.glSamplerParameteriv) es3_functions.glSamplerParameteriv(sampler, pname, param);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if(!current_context) return;
    if(es3_functions.glScissor) es3_functions.glScissor(x, y, width, height);
}

void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels) {
    if(!current_context) return;
    if(es3_functions.glTexImage3D) es3_functions.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels);
}

void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    if(!current_context) return;
    if(es3_functions.glTexStorage2D) es3_functions.glTexStorage2D(target, levels, internalformat, width, height);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
    if(!current_context) return;
    if(es3_functions.glTexStorage3D) es3_functions.glTexStorage3D(target, levels, internalformat, width, height, depth);
}

void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) {
    if(!current_context) return;
    if(es3_functions.glTexSubImage3D) es3_functions.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if(!current_context) return;
    if(es3_functions.glViewport) es3_functions.glViewport(x, y, width, height);
}

void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
    if(!current_context) return;
    if(es3_functions.glDrawArraysInstanced) es3_functions.glDrawArraysInstanced(mode, first, count, instancecount);
}
