/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef POJAVLAUNCHER_PROC_H
#define POJAVLAUNCHER_PROC_H

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#ifdef __APPLE__
/* Apple's libc does not ship C11 <threads.h>. ltw core does not use any C11
 * thread types itself, but the header is transitively required by proc.h's
 * other includes on some toolchains. Use mesa's pthread-based c11 shim that
 * already ships with glsl_optimizer. Android/Linux keep the real <threads.h>. */
#include "c11/threads.h"
#else
#include <threads.h>
#endif

typedef void (*eglMustCastToProperFunctionPointerType)(void);

typedef struct {
#define GLESFUNC(name, type) type name;
#include "es3_functions.h"
#include "es3_extended.h"
#undef GLESFUNC
} es3_functions_t;

extern eglMustCastToProperFunctionPointerType (*host_eglGetProcAddress)(const char *procname);
extern es3_functions_t es3_functions;

#endif //POJAVLAUNCHER_PROC_H
