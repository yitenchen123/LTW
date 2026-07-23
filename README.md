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
LTW dlopens a host `libEGL` at startup. Set the `LIBGL_EGL` environment variable
to the path of the GLES backend's libEGL inside the app bundle:

```
LIBGL_EGL=/var/containers/Bundle/Application/.../Amethyst.app/Frameworks/libEGL.framework/libEGL
```

When using Amethyst's ANGLE or MobileGlues backend, point `LIBGL_EGL` at the
framework's executable. If `LIBGL_EGL` is unset, LTW falls back to
`libEGL.so` (the Android system path, which does not exist on iOS — so the
variable **must** be set).

# Integration
Drop `libltw.dylib` into the app bundle's `Frameworks/` directory. The launcher
should load it as the GL provider (via `dlopen` or LD_PRELOAD equivalent) and
set `LIBGL_EGL` before any GL calls are made.

