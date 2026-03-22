# 2D to 3D Low-Poly Character Generator

This project aims to build a native C++ application that converts 2D character concept art (front, side, and back views) into a textured, low-poly 3D model.

## Core Pipeline & Libraries
1. **Frontend UI**: Dear ImGui + OpenGL to handle image uploads and rendering the 3D model viewer.
2. **AI Inference**: ONNX Runtime (C++ API) to load the Multi-view to 3D AI model (e.g., TripoSR/CRM).
3. **Decimation**: VCGlib (used in MeshLab) to perform Quadric Edge Collapse, preserving shape while reducing polygon count to achieve a "low-poly" aesthetic.
4. **UV Unwrapping**: xatlas to automatically unwrap the low-poly mesh and pack the UV islands.
5. **Texture Projection**: Custom ray-casting via OpenGL and OpenCV to map the 2D reference inputs back onto the 3D UV space, blending the edges for a seamless texture map.

## Project Structure
- `src/main.cpp` - Application entry point, window lifecycle, ImGui rendering.
- `CMakeLists.txt` - CMake configuration linking dependencies.
- `vcpkg.json` - Package manifest handling `imgui`, `glfw3`, etc.
