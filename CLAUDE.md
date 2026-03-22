# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Native C++ application that converts 2D character concept art (front, side, back views) into textured low-poly 3D models. Currently in early development (Phase 1 complete — UI scaffold only).

## Build Commands

Requires CMake 3.20+, a C++17 compiler, and vcpkg. Set `VCPKG_ROOT` to your vcpkg installation path.

```bash
# Configure (from project root)
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build .

# Run
./build/2DTo3DLowPoly
```

vcpkg runs in manifest mode — dependencies are declared in `vcpkg.json` and fetched automatically during configure.

## Architecture

**Pipeline (planned, per plan.md/task.md):**
1. **UI** — ImGui + GLFW + OpenGL: image upload, settings panel, 3D viewport
2. **AI Inference** — ONNX Runtime C++ API: multi-view image → high-poly mesh
3. **Decimation** — VCGlib: Quadric Edge Collapse to target poly count
4. **UV Unwrapping** — xatlas: automatic UV generation
5. **Texture Projection** — OpenCV + ray-casting: project 2D art onto UV space

Currently only step 1 exists. The single source file `src/main.cpp` contains the full application: GLFW window setup, ImGui initialization, and the main render loop with a placeholder control panel.

## Key Dependencies (vcpkg.json)

- `imgui` (with glfw-binding and opengl3-binding features)
- `glfw3`
- `stb`

## Platform Notes

- macOS: Uses OpenGL 3.2 Core Profile with forward compat; links Cocoa/IOKit/CoreVideo frameworks (handled in CMakeLists.txt)
- Linux/Windows: OpenGL 3.0
