## Large Thin Wrapper
A thin OpenGL core-to-OpenGL ES wrapper, primarily intended for running Minecraft.

# Building (Android)
`./gradlew :ltw:assembleRelease`

After completion, an AAR with native libraries will be available in `ltw/build/outputs/aar/ltw-release.aar`

# Building (iOS / macOS / Linux)
LTW ships a CMake build that produces `libltw.dylib` (Apple) / `libltw.so` (Linux)
without needing the Android NDK. All EGL/GLES3 headers are vendored under
`ltw/include/`, so no system GL dev packages are required.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## iOS (cross-compile from macOS)
```bash
cmake -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build-ios -j
# → build-ios/libltw.dylib
```

The GitHub Actions workflow `.github/workflows/ios.yml` does this automatically
and uploads `libltw.dylib` as an artifact on every push.

## Runtime: selecting the GLES backend
At startup LTW dlopens a host EGL provider and resolves every ES3 entry point
through `eglGetProcAddress`.

- **Android**: defaults to `libEGL.so` (system EGL).
- **iOS/macOS**: defaults to `@rpath/libtinygl4angle.dylib` — the gl4es-style
  ANGLE wrapper that links against `libEGL.framework` + `libGLESv2.framework`
  (chromium ANGLE, Metal backend). This matches Amethyst's bundle layout, where
  `libltw.dylib`, `libtinygl4angle.dylib` and the two ANGLE frameworks all live
  under `Amethyst.app/Frameworks/`. The dylib records
  `@executable_path/Frameworks` and `@loader_path/Frameworks` as rpaths, so the
  constructor resolves the wrapper without any environment variable. The
  launcher does **not** need to set `LIBGL_EGL`.

`LIBGL_EGL` remains available as an optional override (e.g. for pointing at a
different GLES backend build during debugging):

```
LIBGL_EGL=/var/containers/Bundle/Application/.../Amethyst.app/Frameworks/libEGL.framework/libEGL
```

# Integration
Drop `libltw.dylib` into the app bundle's `Frameworks/` directory. The launcher
should load it as the GL provider (via `dlopen` or LD_PRELOAD equivalent) and
set `LIBGL_EGL` before any GL calls are made.

