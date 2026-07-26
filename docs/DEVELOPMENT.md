# Development setup

## Common requirements

- Git
- CMake 3.28 or newer
- C++20 compiler
- Qt 6.7 or newer with Qt Widgets and Qt Multimedia
- Ninja for macOS and optional local Windows builds

FFmpeg development libraries are not required for the initial application shell.
They will be pinned with the media-worker spike rather than taken from an
uncontrolled system installation.

The repository contains a vcpkg manifest for the pinned LGPL-compatible FFmpeg
library feature set. GPL and non-free FFmpeg features are intentionally absent.

## Windows

Install Visual Studio 2022 with the Desktop development with C++ workload and a
Qt 6 build for MSVC 2022 x64. Configure Qt discovery through `CMAKE_PREFIX_PATH`
in the user-only `CMakeUserPresets.json` or through the environment; do not commit
machine-specific absolute paths.

Build the complete application:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc --parallel
ctest --preset windows-msvc
```

When Qt is not installed, build and test the platform-neutral core:

```powershell
cmake --preset windows-core
cmake --build --preset windows-core --parallel
ctest --preset windows-core
```

With a local Qt installation and `C:\vcpkg`, the ignored user preset in this
workspace builds the FFmpeg media worker as well:

```powershell
cmake --preset windows-media
cmake --build --preset windows-media --parallel
ctest --preset windows-media
```

## macOS

Install Xcode command-line tools, CMake, Ninja, vcpkg, and a Qt 6 build for
macOS. Set `VCPKG_ROOT` to the vcpkg checkout and make Qt discoverable through
`CMAKE_PREFIX_PATH` or `Qt6_DIR`.

```bash
cmake --preset macos-media
cmake --build --preset macos-media --parallel
ctest --preset macos-media
open build/macos-media/bin/Videx.app
```

The full preset builds the FFmpeg worker, copies it into
`Videx.app/Contents/MacOS`, and deploys Qt frameworks/plugins into the bundle.
Use `macos-clang` only when intentionally building the Qt shell without media
support.

## Optional checks

Enable supported sanitizers in a local preset or configure command:

```text
-DVIDEX_ENABLE_SANITIZERS=ON
```

Run clang-tidy while compiling:

```text
-DVIDEX_ENABLE_CLANG_TIDY=ON
```

Project code treats compiler warnings as errors by default. Third-party code must
not inherit the Videx warning policy.
