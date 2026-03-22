# Ideas for Enhancing the 2D-to-3D Low-Poly Generator

## Priority 1: Biggest Quality Improvements

### 1. MiDaS Depth Estimation (Replace Silhouette Extrusion)
Instead of extruding a flat silhouette, use a monocular depth AI model to get actual 3D geometry from a single image.
- Run **MiDaS v2.1 small** (~30MB ONNX model) via ONNX Runtime C++ API
- Back-project each pixel to 3D using the depth map to create a point cloud
- Run **Poisson Surface Reconstruction** to get a closed, watertight mesh
- Result: real 3D shape with nose, ears, clothing folds — not just a tube
- C++ reference: `github.com/KozhaAkhmet/MiDaS-v2.1-small-cpp`

### 2. Quadric Error Metric (QEM) Decimation
Replace the current simple edge-collapse with the Garland-Heckbert QEM algorithm.
- Uses error quadrics to prioritize which edges to collapse
- Naturally preserves silhouette edges and sharp features
- Produces much better low-poly results at the same triangle count
- C++ reference: `github.com/matthew-rister/mesh_simplification`

### 3. Hard-Edge Flat Shading (The Low-Poly Look)
The single most important aesthetic change:
- Duplicate vertices so each triangle has its own independent normals
- Compute face normal and assign identically to all 3 vertices of each face
- Use `flat` interpolation in the fragment shader
- This gives the classic faceted, crystalline low-poly look (smooth shading looks wrong for low-poly)

### 4. Per-Face Color Baking from Source Image
Instead of per-vertex color interpolation:
- For each triangle face, project its centroid back into the source image
- Sample the image color at that UV and assign as a flat per-face color
- Apply **k-means palette quantization** (k=8-32 colors) to create a stylized limited palette
- This makes models look hand-crafted rather than noisy

---

## Priority 2: Better Background Removal & Preprocessing

### 5. SAM (Segment Anything Model) for Background Removal
Current silhouette extraction uses simple alpha/white threshold — fails on complex images.
- Integrate SAM via ONNX Runtime (~30MB for SAM-Lite)
- User clicks on the character in the UI, SAM masks it perfectly
- Produces clean silhouettes even on complex photos with busy backgrounds
- Alternative: call `rembg` (Python) as a subprocess for quick integration

### 6. Image Preprocessing Pipeline
Before mesh generation:
- **Bilateral filter** — smooths color noise while preserving edges (cleaner face colors)
- **CLAHE histogram equalization** — improves depth estimation on low-contrast images
- **Canny edge detection** — use edges to guide where triangulation points are placed
- These are all one-line OpenCV calls in C++

---

## Priority 3: Advanced Mesh Generation

### 7. Visual Hull / Voxel Carving (Multi-View)
For 3-view mode, replace the current cross-section approach:
- Initialize a 3D voxel grid
- Project each voxel into all silhouette images, discard voxels outside any mask
- Extract mesh from surviving voxels using **Marching Cubes**
- Result: accurate intersection of all views, much better shape
- C++ library: `github.com/unclearness/vacancy` (complete pipeline, no heavy dependencies)

### 8. Contour-Guided 3D Inflation (Single Image)
Better than extrusion for single-image mode:
- Triangulate the 2D silhouette polygon using Constrained Delaunay Triangulation
- Assign depth to each vertex using distance-from-edge falloff (or MiDaS depth map)
- Mirror the front face to create the back, weld edges along the silhouette
- Result: smooth "inflated" mesh with no voxel staircase artifacts

### 9. Edge-Guided Triangulation Point Placement
Key technique for making low-poly models look recognizable:
- Run edge detection on the source image
- Place more triangulation seed points in high-gradient areas (eyes, mouth, clothing seams)
- Place fewer points in flat color regions
- Feed into Delaunay triangulation → project onto 3D mesh
- This aligns triangle edges with visual features automatically
- C++ library: `github.com/delfrrr/delaunator-cpp` (header-only)

---

## Priority 4: UI/UX Improvements

### 10. Better Layout with ImGui Docking
- Use ImGui DockSpace for resizable, rearrangeable panels
- User can resize the 3D viewport vs parameter panels freely
- Save/restore layout between sessions

### 11. Pipeline Step UI
Show the workflow as clear sequential stages:
1. Load Image → 2. Remove Background → 3. Generate Mesh → 4. Decimate → 5. Color → 6. Export
- Each step shown as a tab or collapsible section
- Status indicator (checkmark/spinner) for each step
- Allow jumping back to adjust earlier steps without losing later results

### 12. Real-Time Parameter Preview
- Slider changes for poly count should update the mesh live (or with a short debounce)
- Color palette size slider updates face colors in real time
- Store intermediate mesh states to avoid re-running the full pipeline

### 13. Camera Preset Buttons
Add toolbar buttons: **Front / Side / Top / Perspective** to snap the camera to standard angles. Double-click on a face to set the orbit pivot point there.

### 14. ImGuizmo Integration
- Translate/Rotate/Scale gizmos for positioning the model
- Grid floor rendering for spatial reference
- C++ library: `github.com/CedricGuillemet/ImGuizmo`

### 15. Presets for Quick Results
Buttons that set multiple parameters at once:
- **Minimal** (100-250 tris, 8-color palette) — icon/game-ready
- **Standard** (500-1000 tris, 16-color palette) — balanced low-poly
- **Detailed** (2000-5000 tris, 32-color palette) — high-quality low-poly

### 16. Undo/Redo
Store mesh snapshots in a `std::deque<MeshState>` (last 10 states). Push on every destructive operation (generate, decimate, color change). Makes the tool feel professional.

### 17. Image Preview Thumbnails
Show small thumbnails of loaded front/side/back images in the settings panel. Helps users confirm they loaded the right images.

---

## Priority 5: Export & Output

### 18. Export Formats
- **OBJ + MTL** — universal, works everywhere
- **GLB/GLTF** — web and game engines (Unity, Unreal, Three.js)
- **STL** — 3D printing
- **PLY** — with vertex colors for research/visualization
- C++ libraries: `tinyobjloader` (header-only), `cgltf` (header-only)

### 19. Screenshot / Turntable Export
- One-click screenshot of the current viewport
- Auto-generate a turntable GIF/video (render 36 frames at 10-degree increments)
- Useful for sharing results on social media

---

## Priority 6: Advanced AI Integration (Future)

### 20. TripoSR / InstantMesh for High-Quality Generation
State-of-the-art single-image to 3D:
- **TripoSR**: generates full textured 3D mesh in ~0.5s on GPU (MIT license)
- **InstantMesh**: similar quality, TencentARC
- Both are Python/PyTorch — integrate as a subprocess or local REST API
- Pipeline: generate high-quality mesh externally → import into C++ tool → decimate to low-poly → stylize
- `github.com/VAST-AI-Research/TripoSR`
- `github.com/TencentARC/InstantMesh`

### 21. Depth-Pro / Metric3D for Absolute Depth
Newer models that estimate metric depth (actual scale in meters) rather than relative:
- Better proportions for generated 3D models
- ONNX export available

---

## Recommended C++ Libraries

| Purpose | Library | Type |
|---|---|---|
| Depth estimation | MiDaS v2.1 small (ONNX) | AI model (~30MB) |
| AI inference | ONNX Runtime C++ API | vcpkg available |
| Background removal | SAM-Lite (ONNX) | AI model (~30MB) |
| Image processing | OpenCV | vcpkg available |
| Mesh simplification | QEM (Garland-Heckbert) | C++ source |
| Geometry processing | libigl | Header-only |
| UV unwrapping | xatlas | Already integrated |
| Delaunay triangulation | delaunator-cpp | Header-only |
| Marching Cubes | MeshReconstruction | No dependencies |
| Voxel carving | Vacancy | C++ library |
| 3D gizmos | ImGuizmo | Built on ImGui |
| OBJ export | tinyobjloader | Header-only |
| GLTF export | cgltf | Header-only |

---

## Suggested Implementation Order

1. Hard-edge flat shading (#3) — instant visual improvement, easy
2. Per-face color baking + palette quantization (#4) — makes models look good
3. QEM decimation (#2) — better mesh quality at same poly count
4. Export to OBJ (#18) — users can use their models
5. Image preview thumbnails (#17) — quick UX win
6. Camera preset buttons (#13) — quick UX win
7. MiDaS depth integration (#1) — major quality leap
8. Background removal with SAM (#5) — cleaner inputs
9. Visual hull for 3-view (#7) — better multi-view results
10. Presets + real-time preview (#15, #12) — polish
