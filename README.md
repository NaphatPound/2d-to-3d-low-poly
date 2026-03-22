# 2D to 3D Low-Poly Character Generator

## Prerequisites
- **CMake** (3.20+)
- **vcpkg** (for dependency management)
- A **C++17** compatible compiler (Apple Clang on macOS, GCC on Linux, or MSVC on Windows).

## Generating the Build Files & Compiling

This project uses `vcpkg` in manifest mode to manage dependencies (`imgui`, `glfw3`, `stb`).

1. **Clone and Setup vcpkg** (if not already installed):
   ```bash
   git clone https://github.com/microsoft/vcpkg.git
   ./vcpkg/bootstrap-vcpkg.sh
   export VCPKG_ROOT=$(pwd)/vcpkg
   ```

2. **Configure the Project**:
   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
   ```
   *(CMake will automatically download and build the dependencies specified in `vcpkg.json`.)*

3. **Build the Project**:
   ```bash
   cmake --build .
   ```

4. **Run**:
   ```bash
   ./2DTo3DLowPoly
   ```

## Architecture Roadmap
1. **Frontend**: ImGui + OpenGL for the upload screens and 3D visualization.
2. **AI Inference**: ONNX Runtime or LibTorch.
3. **Decimation**: VCGlib for shrinking high-poly meshes to low-poly.
4. **UV Unwrapping**: xatlas.
5. **Texture Projection**: OpenCV mapping back onto the unwrapped UVs.
