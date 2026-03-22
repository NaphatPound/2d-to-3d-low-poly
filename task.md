# Project Tasks

- [x] **Phase 1: Project Setup and UI**
  - [x] Initialize CMake project structure.
  - [x] Integrate Dear ImGui, OpenGL, and GLFW.
  - [x] Set up package management (`vcpkg.json`).
  - [x] Create layout panels and the 3D viewport skeleton.

- [ ] **Phase 2: AI Inference Integration (ONNX Runtime)**
  - [ ] Integrate ONNX Runtime C++ API.
  - [ ] Prepare an Image-to-3D model in ONNX format.
  - [ ] Implement OpenCV preprocessing (background removal).
  - [ ] Run inference to generate the raw high-poly mesh.

- [ ] **Phase 3: Mesh Decimation (VCGlib)**
  - [ ] Integrate VCGlib.
  - [ ] Implement Quadric Edge Collapse decimation.
  - [ ] Add ImGui controls for the user to select their desired polygon count.

- [ ] **Phase 4: UV Unwrapping (xatlas)**
  - [ ] Integrate the xatlas library.
  - [ ] Pass the decimated low-poly mesh to xatlas to generate its UV map.

- [ ] **Phase 5: Texture Projection and Baking**
  - [ ] Ray-cast from camera angles matching the Front, Side, and Back images.
  - [ ] Use OpenCV math to project colors onto the UV space.
  - [ ] Blend intersecting angles and bake into a definitive texture.
  - [ ] Render the finalized asset.
