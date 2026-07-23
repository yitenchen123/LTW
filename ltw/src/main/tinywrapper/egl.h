/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef POJAVLAUNCHER_EGL_H
#define POJAVLAUNCHER_EGL_H

#include <stdbool.h>
#include <EGL/egl.h>
#include "proc.h"
#include "unordered_map/unordered_map.h"

#define MAX_BOUND_BUFFERS 9
#define MAX_BOUND_BASEBUFFERS 4
#define MAX_DRAWBUFFERS 8
#define MAX_FBTARGETS 8
#define MAX_TMUS 8
#define MAX_TEXTARGETS 8

typedef struct {
    bool ready;
    GLuint indirectRenderBuffer;
} basevertex_renderer_t;

typedef struct {
    bool available;
    PFNGLBLENDEQUATIONIPROC blendequationi;
    PFNGLBLENDEQUATIONSEPARATEIPROC blendequationseparatei;
    PFNGLBLENDFUNCIPROC blendfunci;
    PFNGLBLENDFUNCSEPARATEIPROC blendfuncseparatei;
    PFNGLCOLORMASKIPROC colormaski;
} blending_functions_t;

typedef struct {
    GLuint index;
    GLuint buffer;
    bool ranged;
    GLintptr  offset;
    GLintptr  size;
} basebuffer_binding_t;

typedef struct {
    GLuint color_targets[MAX_FBTARGETS];
    GLuint color_objects[MAX_FBTARGETS];
    GLuint color_levels[MAX_FBTARGETS];
    GLuint color_layers[MAX_FBTARGETS];
    GLenum virt_drawbuffers[MAX_DRAWBUFFERS];
    GLenum phys_drawbuffers[MAX_DRAWBUFFERS];
    GLsizei nbuffers;
} framebuffer_t;

typedef struct {
    bool ready;
    GLuint temp_texture;
    GLuint tempfb;
    GLuint destfb;
    void* depthData;
    GLsizei depthWidth, depthHeight;
} framebuffer_copier_t;

typedef struct {
    GLenum original_swizzle[4];
    GLboolean goofy_byte_order;
    GLboolean upload_bgra;
} texture_swizzle_track_t;

typedef struct {
    EGLContext phys_context;
    bool context_rdy;
    bool es31, es32, buffer_storage, buffer_texture_ext, multidraw_indirect, timer_query;
    // When the host GLES backend lacks GL_EXT_texture_buffer (ES 3.0 only),
    // LTW transparently emulates buffer textures with 2D textures: the shader
    // is lowered (samplerBuffer -> sampler2D, see shader_wrapper.c) and the
    // GL_TEXTURE_BUFFER bind target / glTexBuffer calls are intercepted
    // (see main.c). GL_ARB_texture_buffer_object is still advertised so MC's
    // CPU-side extension checks pass and it proceeds down the buffer-texture
    // code path, which LTW then services via the emulation.
    bool emulate_texture_buffer;
    GLint shader_version;
    basevertex_renderer_t basevertex;
    PFNGLDRAWELEMENTSBASEVERTEXPROC drawelementsbasevertex;
    blending_functions_t blending;
    GLuint multidraw_element_buffer;
    framebuffer_copier_t framebuffer_copier;
    unordered_map* shader_map;
    unordered_map* program_map;
    unordered_map* framebuffer_map;
    unordered_map* texture_swztrack_map;
    // buffer-texture name (GLuint created by MC for GL_TEXTURE_BUFFER) ->
    // internal GL_TEXTURE_2D name used by the emulation.
    unordered_map* texbuf_emul_map;
    // currently bound GL_TEXTURE_BUFFER name (single active binding tracker).
    GLuint bound_buf_texture;
    unordered_map* bound_basebuffers[MAX_BOUND_BASEBUFFERS];
    int proxy_width, proxy_height, proxy_intformat, maxTextureSize;
    GLint max_drawbuffers;
    GLuint bound_buffers[MAX_BOUND_BUFFERS];
    GLuint program;
    GLuint draw_framebuffer;
    GLuint read_framebuffer;
    char* extensions_string;
    size_t nextras;
    int nextensions_es;
    char** extra_extensions_array;
} context_t;

extern thread_local context_t *current_context;
extern void init_egl();
extern GLenum get_textarget_query_param(GLenum target);

#endif //POJAVLAUNCHER_EGL_H
