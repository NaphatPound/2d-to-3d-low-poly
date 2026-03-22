#pragma once

#include "viewport.h"
#include "app.h"
#include <string>
#include <vector>
#include <functional>

// Image data loaded from disk
struct ImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
};

// Texture atlas output
struct TextureAtlas {
    std::vector<unsigned char> pixels; // RGBA
    int width = 0;
    int height = 0;
};

// Full pipeline result
struct PipelineResult {
    Mesh mesh;
    TextureAtlas texture;
    bool success = false;
    std::string error;
};

// Callback for logging from within the pipeline
using LogCallback = std::function<void(const std::string& msg, int level)>; // level: 0=info, 1=success, 2=warn, 3=error

// Load an image from disk using stb_image
ImageData load_image(const std::string& path);

// Extract a binary silhouette mask from an image (white = foreground)
std::vector<bool> extract_silhouette(const ImageData& img, int threshold = 240);

// Generate a 3D mesh by extruding the front silhouette and carving with the side silhouette
Mesh generate_mesh_from_silhouettes(const ImageData& front, const ImageData& side, const ImageData& back, LogCallback log);

// Decimate a mesh to a target triangle count using edge collapse
Mesh decimate_mesh(const Mesh& input, int target_triangles, LogCallback log);

// UV unwrap using xatlas
bool uv_unwrap(Mesh& mesh, LogCallback log);

// Bake texture from source images onto UV-mapped mesh
TextureAtlas bake_texture(const Mesh& mesh, const ImageData& front, const ImageData& side, const ImageData& back, LogCallback log);

// Generate mesh from a single front image (assumes symmetric side/back)
Mesh generate_mesh_from_front(const ImageData& front, LogCallback log);

// Convert mesh to flat-shaded (duplicate vertices per face, assign face normals)
Mesh make_flat_shaded(const Mesh& input);

// Bake per-face color by projecting face centroids into source image
void bake_face_colors(Mesh& mesh, const ImageData& front, const ImageData* side = nullptr, const ImageData* back = nullptr);

// Quantize mesh vertex colors to k colors using k-means
void quantize_colors(Mesh& mesh, int k = 16);

// Laplacian smoothing on a mesh
void smooth_mesh(Mesh& mesh, int iterations = 3, float lambda = 0.5f);

// Auto retopology: resample mesh surface with evenly distributed vertices + Delaunay
Mesh retopologize(const Mesh& input, int target_vertices, LogCallback log);

// Run the full pipeline (3-view)
PipelineResult run_pipeline(const std::string& front_path, const std::string& side_path,
                            const std::string& back_path, int target_poly_count, LogCallback log);

// Run pipeline from front image only
PipelineResult run_pipeline_front_only(const std::string& front_path, int target_poly_count, LogCallback log);
