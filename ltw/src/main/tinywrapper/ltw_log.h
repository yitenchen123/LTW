/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 *
 * Portable logging shim. On Android it forwards to liblog; on every other
 * platform (iOS / macOS / Linux) it falls back to vfprintf(stderr) so the
 * rest of LTW keeps working without an Android NDK in the toolchain.
 */
#ifndef LTW_LOG_H
#define LTW_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#if defined(__ANDROID__)
#include <android/log.h>
#define LTW_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LTWInit", __VA_ARGS__)
#define LTW_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "LTW",       __VA_ARGS__)
#define LTW_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "LTW",       __VA_ARGS__)
#else
/* Apple platforms (iOS/macOS) and desktop Linux: route through stderr so
 * the output shows up in the launcher's log capture exactly like printf
 * already does elsewhere in LTW. */
#define LTW_LOGE(...) do { fprintf(stderr, "LTWInit: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while (0)
#define LTW_LOGW(...) do { fprintf(stderr, "LTW: ");      fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while (0)
#define LTW_LOGI(...) do { fprintf(stderr, "LTW: ");      fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while (0)
#endif

#endif /* LTW_LOG_H */
