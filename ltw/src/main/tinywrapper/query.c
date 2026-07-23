//
// Created by whbex on 25.11.2025.
//

#include <GL/gl.h>
#include <GL/glext.h>
#include "egl.h"
#include "proc.h"

#define CTX_CHECK() if (!current_context) return;

// GLES 3.0 core query functions. They are loaded into es3_functions via
// host_eglGetProcAddress at init, but LTW would otherwise expose empty
// STUBFUNCs through eglGetProcAddress("glGenQueries") etc. -- ANGLE need not
// export core static entries through the proc-address lookup (same root cause
// as the glGetFloatv fix), so MC gets a no-op stub and crashes on the first
// glGenQueries / glBeginQuery call. These thin wrappers forward to the host
// pointer that was already resolved at init.

void glGenQueries(GLsizei n, GLuint* ids) {
    CTX_CHECK();
    if(es3_functions.glGenQueries) es3_functions.glGenQueries(n, ids);
}

void glDeleteQueries(GLsizei n, const GLuint* ids) {
    CTX_CHECK();
    if(es3_functions.glDeleteQueries) es3_functions.glDeleteQueries(n, ids);
}

GLboolean glIsQuery(GLuint id) {
    if(!current_context) return GL_FALSE;
    if(es3_functions.glIsQuery) return es3_functions.glIsQuery(id);
    return GL_FALSE;
}

void glBeginQuery(GLenum target, GLuint id) {
    CTX_CHECK();
    if(es3_functions.glBeginQuery) es3_functions.glBeginQuery(target, id);
}

void glEndQuery(GLenum target) {
    CTX_CHECK();
    if(es3_functions.glEndQuery) es3_functions.glEndQuery(target);
}

void glGetQueryiv(GLenum target, GLenum pname, GLint* params) {
    CTX_CHECK();
    if(es3_functions.glGetQueryiv) es3_functions.glGetQueryiv(target, pname, params);
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
    CTX_CHECK();
    if(es3_functions.glGetQueryObjectuiv) es3_functions.glGetQueryObjectuiv(id, pname, params);
}

// Minecraft uses these only for timer queries
void glGetQueryObjecti64v(GLuint id, GLenum pname, int64_t* params){
    CTX_CHECK();
    // May be not needed, added just in case
    if(!current_context->timer_query){
        *params = 1;
        return;
    }
   es3_functions.glGetQueryObjecti64vEXT(id, pname, params);
}

void glGetQueryObjectui64v(GLuint id, GLenum pname, uint64_t* params){
    CTX_CHECK();
    if(!current_context->timer_query){
        *params = 1;
        return;
    }
    es3_functions.glGetQueryObjectui64vEXT(id, pname, params);
}
void glQueryCounter(GLuint id, GLenum target){
    if(!current_context || !current_context->timer_query)
        return;
    es3_functions.glQueryCounterEXT(id, target);
}

// Moved from main.c
void glGetQueryObjectiv( 	GLuint id,
                            GLenum name,
                            GLint * params) {
    CTX_CHECK();
    // This is not recommended but i don't care
    es3_functions.glGetQueryObjectuiv(id, name, (GLuint*)params);
}