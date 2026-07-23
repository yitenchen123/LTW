/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include "egl.h"
#include "unordered_map/int_hash.h"
#include "string_utils.h"
#include "env.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_local context_t *current_context;
unordered_map* context_map;

EGLContext (*host_eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLBoolean (*host_eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
EGLBoolean (*host_eglMakeCurrent) (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);

void init_egl() {
    context_map = alloc_intmap();
    host_eglCreateContext = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                            const EGLint *)) host_eglGetProcAddress("eglCreateContext");
    host_eglDestroyContext = (EGLBoolean (*)(EGLDisplay, EGLContext)) host_eglGetProcAddress(
            "eglDestroyContext");
    host_eglMakeCurrent = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface,
                                          EGLContext)) host_eglGetProcAddress("eglMakeCurrent");
}

static bool init_context(context_t* tw_context) {
    tw_context->shader_map = alloc_intmap_safe();
    if(!tw_context->shader_map) goto fail;
    tw_context->framebuffer_map = alloc_intmap_safe();
    if(!tw_context->framebuffer_map) goto fail_dealloc;
    tw_context->program_map = alloc_intmap_safe();
    if(!tw_context->program_map) goto fail_dealloc;
    tw_context->texture_swztrack_map = alloc_intmap_safe();
    if(!tw_context->texture_swztrack_map) goto fail_dealloc;
    for(int i = 0; i < MAX_BOUND_BASEBUFFERS; i++) {
        unordered_map *map = alloc_intmap_safe();
        if(!map) goto fail_dealloc;
        tw_context->bound_basebuffers[i] = map;
    }
    return true;

    fail_dealloc:
    for(int i = 0; i < MAX_BOUND_BASEBUFFERS; i++) {
        unordered_map *map = tw_context->bound_basebuffers[i];
        if(map) unordered_map_free(map);
    }
    if(tw_context->shader_map)
        unordered_map_free(tw_context->shader_map);
    if(tw_context->framebuffer_map)
        unordered_map_free(tw_context->framebuffer_map);
    if(tw_context->program_map)
        unordered_map_free(tw_context->program_map);
    if(tw_context->texture_swztrack_map)
        unordered_map_free(tw_context->texture_swztrack_map);
    fail:
    return false;
}

static void free_context(context_t* tw_context) {
    unordered_map_free(tw_context->shader_map);
    unordered_map_free(tw_context->program_map);
    unordered_map_free(tw_context->framebuffer_map);
    unordered_map_free(tw_context->texture_swztrack_map);
    if(tw_context->extensions_string != NULL) free(tw_context->extensions_string);
    if(tw_context->nextras != 0 && tw_context->extra_extensions_array != NULL) {
        for(int i = 0; i < tw_context->nextras; i++) {
            free((tw_context->extra_extensions_array[i]));
        }
        free(tw_context->extra_extensions_array);
    }
}

// On ES 3.0+, glGetString(GL_EXTENSIONS) is deprecated and some Metal-backed
// drivers (ANGLE, MobileGlues) return NULL or an empty string. Fall back to
// the spec-compliant glGetStringi(GL_EXTENSIONS, i) enumeration so LTW -- and
// through it Minecraft -- sees the real capability set. Detection-only: no
// emulation, no shader rewriting.
static char* build_host_extensions() {
    const char* ext = (const char*) es3_functions.glGetString(GL_EXTENSIONS);
    if(ext && ext[0]) {
        char* copy = malloc(strlen(ext) + 1);
        strcpy(copy, ext);
        return copy;
    }
    GLint num = 0;
    es3_functions.glGetIntegerv(GL_NUM_EXTENSIONS, &num);
    if(num <= 0) {
        char* empty = malloc(1);
        empty[0] = 0;
        return empty;
    }
    size_t total = 1;
    for(GLint i = 0; i < num; i++) {
        const GLubyte* e = es3_functions.glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if(e) total += strlen((const char*)e) + 1;
    }
    char* combined = malloc(total);
    combined[0] = 0;
    for(GLint i = 0; i < num; i++) {
        const GLubyte* e = es3_functions.glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if(e) {
            if(combined[0]) strcat(combined, " ");
            strcat(combined, (const char*)e);
        }
    }
    return combined;
}

void init_extra_extensions(context_t* context, int* length) {
    context->extensions_string = build_host_extensions();
    *length = (int)strlen(context->extensions_string);
}

void add_extra_extension(context_t* context, int* length, const char* extension)  {
    size_t extension_len = strlen(extension);

    char str_append_extension[extension_len + 2];
    memcpy(str_append_extension, extension, extension_len);
    str_append_extension[extension_len] = ' ';
    str_append_extension[extension_len + 1] = 0;
    context->extensions_string = gl4es_append(context->extensions_string, length, str_append_extension);

    int extension_idx = context->nextras++;
    context->extra_extensions_array = realloc(context->extra_extensions_array, sizeof(char*)*context->nextras);
    char* extra_extension = malloc(extension_len + 1);
    strncpy(extra_extension, extension, extension_len + 1);
    context->extra_extensions_array[extension_idx] = extra_extension;
}

void fin_extra_extensions(context_t* context, int length) {
    if(context->extensions_string[length-2] != ' ') return;
    char* orig_string = context->extensions_string;
    context->extensions_string = realloc(context->extensions_string, length - 1);
    if(context->extensions_string == NULL) {
        free(orig_string);
        return;
    }
    context->extensions_string[length-2] = 0;
}

void build_extension_string(context_t* context) {
    int length;
    init_extra_extensions(context, &length);
    if(context->buffer_storage) {
        if(!env_istrue("LTW_HIDE_BUFFER_STORAGE"))
            add_extra_extension(context, &length, "GL_ARB_buffer_storage");
        else printf("LTW: The buffer storage extension is hidden.\n");
    }
    if(context->buffer_texture_ext || context->es32) {
        add_extra_extension(context, &length, "GL_ARB_texture_buffer_object");
    }
    add_extra_extension(context, &length, "GL_ARB_draw_elements_base_vertex");
    // Required by Iris. Indexed variants are available since ES3.2 or with OES/EXT_draw_buffers_indexed extensions
    if(context->blending.available)
        add_extra_extension(context, &length, "GL_ARB_draw_buffers_blend");
    // Used by Minecraft for the GPU usage counter (see Blaze3D TimerQuery)
    add_extra_extension(context, &length, "GL_ARB_timer_query");
    // More extensions are possible, but will need way more wraps and tracking.
    fin_extra_extensions(context, length);
}

static void find_esversion(context_t* context) {
    const char* version = (const char*) es3_functions.glGetString(GL_VERSION);
    const char* shader_version = (const char*) es3_functions.glGetString(GL_SHADING_LANGUAGE_VERSION);

    int esmajor = 0, esminor = 0, shadermajor = 3, shaderminor = 0;
    sscanf(version, " OpenGL ES %i.%i", &esmajor, &esminor);
    sscanf(shader_version, " OpenGL ES GLSL ES %i.%i", &shadermajor, &shaderminor);
    context->shader_version = shadermajor * 100 + shaderminor;
    printf("LTW: Running on OpenGL ES %i.%i with ESSL %i\n", esmajor, esminor, context->shader_version);
    if(esmajor == 0 && esminor == 0) goto fail;
    if(esmajor < 3 || context->shader_version < 300) {
        printf("Unsupported OpenGL ES version. This will cause you problems down the line.\n");
        return;
    }
    if(esmajor == 3) {
        context->es31 = esminor >= 1;
        context->es32 = esminor >= 2;
    }else if(esmajor > 3) {
        context->es32 = context->es31 = true;
    }

    char* extensions = build_host_extensions();
    printf("LTW: Host extensions: %.800s\n", extensions);
    if(strstr(extensions, "GL_EXT_buffer_storage")) context->buffer_storage = true;
    if(strstr(extensions, "GL_EXT_texture_buffer")) context->buffer_texture_ext = true;
    if(strstr(extensions, "GL_EXT_multi_draw_indirect")) context->multidraw_indirect = true;

    // ANGLE supports GL_ANGLE_request_extension: at runtime the app can ask
    // ANGLE to enable extensions it implements but doesn't expose by default.
    // If GL_EXT_texture_buffer isn't in the default string but ANGLE's request
    // mechanism is available, ask ANGLE to enable it natively. This makes
    // ANGLE's own GLSL compiler accept #extension GL_EXT_texture_buffer /
    // samplerBuffer -- no LTW-side emulation or shader rewriting needed.
    // If ANGLE doesn't implement the extension for this backend, the request
    // is a no-op and buffer_texture_ext stays false.
    if(!context->buffer_texture_ext && es3_functions.glRequestExtensionANGLE) {
        printf("LTW: Requesting GL_EXT_texture_buffer via GL_ANGLE_request_extension\n");
        es3_functions.glRequestExtensionANGLE("GL_EXT_texture_buffer");
        free(extensions);
        extensions = build_host_extensions();
        if(strstr(extensions, "GL_EXT_texture_buffer")) {
            context->buffer_texture_ext = true;
            printf("LTW: GL_EXT_texture_buffer enabled via ANGLE request_extension\n");
        } else {
            printf("LTW: ANGLE did not enable GL_EXT_texture_buffer (not implemented for this backend)\n");
        }
    }

    // Manual override for backends that support the extension at the GLSL level
    // but neither advertise it nor implement the request mechanism.
    if(env_istrue_d("LTW_FORCE_TEXTURE_BUFFER", false)) context->buffer_texture_ext = true;
    printf("LTW: buffer_texture_ext=%d es31=%d es32=%d\n",
           context->buffer_texture_ext, context->es31, context->es32);

    // EXT_disjoint_timer_query provides accurate int64 timer queries
    // on Core Profile it's ARB_timer_query instead
    // This enables real time queries via mentioned extension, otherwise faked ones are used (see query.c)
    if(strstr(extensions, "GL_EXT_disjoint_timer_query") || env_istrue_d("LTW_ENABLE_TIMER_QUERY", false)) context->timer_query = true;

    bool basevertex_oes = strstr(extensions, "GL_OES_draw_elements_base_vertex");
    bool basevertex_ext = strstr(extensions, "GL_EXT_draw_elements_base_vertex");
    if(context->es32) context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertex;
    else if(basevertex_oes) context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertexOES;
    else if(basevertex_ext) context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertexEXT;
    else context->drawelementsbasevertex = NULL;

    bool drawbuffersi_oes = strstr(extensions, "GL_OES_draw_buffers_indexed");
    bool drawbuffersi_ext = strstr(extensions, "GL_EXT_draw_buffers_indexed");
    blending_functions_t* blend = &context->blending;
    blend->available = true;
#define SET_FUNC(type) \
    blend->blendequationi = es3_functions.glBlendEquationi ## type; \
    blend->blendequationseparatei = es3_functions.glBlendEquationSeparatei ## type; \
    blend->blendfunci = es3_functions.glBlendFunci ## type; \
    blend->blendfuncseparatei = es3_functions.glBlendFuncSeparatei ## type; \
    blend->colormaski = es3_functions.glColorMaski ## type; \

    if(context->es32){
        SET_FUNC()
    }
    else if(drawbuffersi_oes){
        SET_FUNC(OES)
    }
    else if(drawbuffersi_ext){
        SET_FUNC(EXT)
    }
    else {
        blend->available = false;
    }
#undef SET_FUNC
    free(extensions);
    build_extension_string(context);

    return;
    fail:
    printf("LTW: Failed to detect OpenGL ES version");
}

void basevertex_init(context_t* context);
void buffer_copier_init(context_t* context);
static void init_incontext(context_t* tw_context) {
    es3_functions.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tw_context->maxTextureSize);
    es3_functions.glGetIntegerv(GL_MAX_DRAW_BUFFERS, &tw_context->max_drawbuffers);
    es3_functions.glGetIntegerv(GL_NUM_EXTENSIONS, &tw_context->nextensions_es);
    if(tw_context->max_drawbuffers > MAX_DRAWBUFFERS) {
        tw_context->max_drawbuffers = MAX_DRAWBUFFERS;
    }

    find_esversion(tw_context);

    basevertex_init(tw_context);
    buffer_copier_init(tw_context);
    es3_functions.glGenBuffers(1, &tw_context->multidraw_element_buffer);
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    EGLContext phys_context = host_eglCreateContext(dpy, config, share_context, attrib_list);
    if(phys_context == EGL_NO_CONTEXT) return phys_context;
    context_t* tw_context = calloc(1, sizeof(context_t));
    if(tw_context == NULL || !init_context(tw_context)) {
        if(tw_context) free(tw_context);
        host_eglDestroyContext(dpy, phys_context);
        return EGL_NO_CONTEXT;
    }
    unordered_map_put(context_map, phys_context, tw_context);
    return phys_context;
}

EGLBoolean eglDestroyContext (EGLDisplay dpy, EGLContext ctx) {
    if(!host_eglDestroyContext(dpy, ctx)) return EGL_FALSE;
    context_t* old_ctx = unordered_map_remove(context_map, ctx);
    free_context(old_ctx);
    free(old_ctx);
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    if(!host_eglMakeCurrent(dpy, draw, read, ctx)) return EGL_FALSE;
    if(ctx == EGL_NO_CONTEXT) {
        current_context = NULL;
        return EGL_TRUE;
    }
    context_t* tw_context = unordered_map_get(context_map, ctx);
    if(tw_context == NULL) {
        printf("TinywrapperEGL: Failed to find context %p\n", ctx);
        abort();
    }
    if(!tw_context->context_rdy) {
        init_incontext(tw_context);
        tw_context->context_rdy = true;
    }
    current_context = tw_context;
    return EGL_TRUE;
}
