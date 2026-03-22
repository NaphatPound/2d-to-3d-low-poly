#include "depth.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

// ============================================================
// Depth map smoothing (box blur)
// ============================================================

static std::vector<float> smooth_depth(const std::vector<float>& depth, int w, int h, int radius) {
    std::vector<float> tmp(w * h, 0);
    std::vector<float> out(w * h, 0);

    // Horizontal pass
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float sum = 0; int count = 0;
            for (int dx = -radius; dx <= radius; dx++) {
                int nx = x + dx;
                if (nx >= 0 && nx < w) {
                    sum += depth[y * w + nx];
                    count++;
                }
            }
            tmp[y * w + x] = sum / count;
        }
    }
    // Vertical pass
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float sum = 0; int count = 0;
            for (int dy = -radius; dy <= radius; dy++) {
                int ny = y + dy;
                if (ny >= 0 && ny < h) {
                    sum += tmp[ny * w + x];
                    count++;
                }
            }
            out[y * w + x] = sum / count;
        }
    }
    return out;
}

// Dilate a boolean mask to fill small holes
static std::vector<bool> dilate_mask(const std::vector<bool>& mask, int w, int h, int radius) {
    std::vector<bool> out(w * h, false);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (mask[y * w + x]) { out[y * w + x] = true; continue; }
            bool found = false;
            for (int dy = -radius; dy <= radius && !found; dy++) {
                for (int dx = -radius; dx <= radius && !found; dx++) {
                    int ny = y + dy, nx = x + dx;
                    if (ny >= 0 && ny < h && nx >= 0 && nx < w && mask[ny * w + nx])
                        found = true;
                }
            }
            out[y * w + x] = found;
        }
    }
    return out;
}

// ============================================================
// MiDaS depth estimation via ONNX Runtime
// ============================================================

std::vector<float> estimate_depth(const ImageData& img, const std::string& model_path, LogCallback log) {
    log("Loading MiDaS model: " + model_path, 0);

    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "midas");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(4);
        Ort::Session session(env, model_path.c_str(), opts);

        const int midas_size = 256;

        std::vector<float> input_tensor(1 * 3 * midas_size * midas_size);

        for (int y = 0; y < midas_size; y++) {
            for (int x = 0; x < midas_size; x++) {
                int src_x = x * img.width / midas_size;
                int src_y = y * img.height / midas_size;
                src_x = std::clamp(src_x, 0, img.width - 1);
                src_y = std::clamp(src_y, 0, img.height - 1);
                int src_idx = (src_y * img.width + src_x) * 4;

                float r = img.pixels[src_idx + 0] / 255.0f;
                float g = img.pixels[src_idx + 1] / 255.0f;
                float b = img.pixels[src_idx + 2] / 255.0f;

                r = (r - 0.485f) / 0.229f;
                g = (g - 0.456f) / 0.224f;
                b = (b - 0.406f) / 0.225f;

                input_tensor[0 * midas_size * midas_size + y * midas_size + x] = r;
                input_tensor[1 * midas_size * midas_size + y * midas_size + x] = g;
                input_tensor[2 * midas_size * midas_size + y * midas_size + x] = b;
            }
        }

        std::array<int64_t, 4> input_shape = {1, 3, midas_size, midas_size};
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_val = Ort::Value::CreateTensor<float>(
            memory_info, input_tensor.data(), input_tensor.size(),
            input_shape.data(), input_shape.size());

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session.GetInputNameAllocated(0, allocator);
        auto output_name = session.GetOutputNameAllocated(0, allocator);
        const char* input_names[] = {input_name.get()};
        const char* output_names[] = {output_name.get()};

        log("Running MiDaS inference...", 0);
        auto output_vals = session.Run(Ort::RunOptions{nullptr}, input_names, &input_val, 1, output_names, 1);

        float* output_data = output_vals[0].GetTensorMutableData<float>();
        auto output_shape = output_vals[0].GetTensorTypeAndShapeInfo().GetShape();
        int out_h = (int)output_shape[output_shape.size() - 2];
        int out_w = (int)output_shape[output_shape.size() - 1];

        std::vector<float> depth(img.width * img.height);
        float min_d = 1e30f, max_d = -1e30f;
        for (int y = 0; y < img.height; y++) {
            for (int x = 0; x < img.width; x++) {
                int sy = y * out_h / img.height;
                int sx = x * out_w / img.width;
                sy = std::clamp(sy, 0, out_h - 1);
                sx = std::clamp(sx, 0, out_w - 1);
                float d = output_data[sy * out_w + sx];
                depth[y * img.width + x] = d;
                min_d = std::min(min_d, d);
                max_d = std::max(max_d, d);
            }
        }

        // Normalize to [0, 1]
        if (max_d > min_d) {
            for (auto& d : depth) d = (d - min_d) / (max_d - min_d);
        }

        // Smooth the depth map to remove noise (3 passes)
        log("Smoothing depth map...", 0);
        for (int i = 0; i < 3; i++) {
            depth = smooth_depth(depth, img.width, img.height, 3);
        }

        log("Depth estimated and smoothed", 1);
        return depth;

    } catch (const Ort::Exception& e) {
        log("ONNX Runtime error: " + std::string(e.what()), 3);
        return {};
    }
}

// ============================================================
// Laplacian mesh smoothing
// ============================================================

static void laplacian_smooth(Mesh& mesh, int iterations, float lambda) {
    int n = (int)mesh.vertices.size();
    // Build adjacency: for each vertex, list of neighbor vertices
    std::vector<std::vector<int>> adj(n);
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        int a = mesh.indices[i], b = mesh.indices[i+1], c = mesh.indices[i+2];
        adj[a].push_back(b); adj[a].push_back(c);
        adj[b].push_back(a); adj[b].push_back(c);
        adj[c].push_back(a); adj[c].push_back(b);
    }
    // Remove duplicate neighbors
    for (auto& neighbors : adj) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    for (int iter = 0; iter < iterations; iter++) {
        std::vector<float> new_pos(n * 3);
        for (int i = 0; i < n; i++) {
            if (adj[i].empty()) {
                new_pos[i*3+0] = mesh.vertices[i].pos[0];
                new_pos[i*3+1] = mesh.vertices[i].pos[1];
                new_pos[i*3+2] = mesh.vertices[i].pos[2];
                continue;
            }
            float avg[3] = {0, 0, 0};
            for (int j : adj[i]) {
                avg[0] += mesh.vertices[j].pos[0];
                avg[1] += mesh.vertices[j].pos[1];
                avg[2] += mesh.vertices[j].pos[2];
            }
            float inv = 1.0f / adj[i].size();
            avg[0] *= inv; avg[1] *= inv; avg[2] *= inv;
            new_pos[i*3+0] = mesh.vertices[i].pos[0] + lambda * (avg[0] - mesh.vertices[i].pos[0]);
            new_pos[i*3+1] = mesh.vertices[i].pos[1] + lambda * (avg[1] - mesh.vertices[i].pos[1]);
            new_pos[i*3+2] = mesh.vertices[i].pos[2] + lambda * (avg[2] - mesh.vertices[i].pos[2]);
        }
        for (int i = 0; i < n; i++) {
            mesh.vertices[i].pos[0] = new_pos[i*3+0];
            mesh.vertices[i].pos[1] = new_pos[i*3+1];
            mesh.vertices[i].pos[2] = new_pos[i*3+2];
        }
    }
}

// ============================================================
// Depth map to 3D mesh (improved)
// ============================================================

Mesh depth_to_mesh(const ImageData& img, const std::vector<float>& depth, int downsample, LogCallback log) {
    if (depth.empty()) return {};

    log("Converting depth map to 3D mesh...", 0);

    int w = img.width;
    int h = img.height;
    int sw = w / downsample;
    int sh = h / downsample;

    // Dilate mask to fill small holes in silhouette
    auto mask = extract_silhouette(img);
    mask = dilate_mask(mask, w, h, downsample);

    Mesh mesh;

    // Preserve aspect ratio: fit model so the longest side = 2 units
    float aspect = (float)sw / (float)sh;
    float model_height, model_width;
    if (aspect > 1.0f) {
        // wider than tall
        model_width = 2.0f;
        model_height = 2.0f / aspect;
    } else {
        // taller than wide (most character images)
        model_height = 2.0f;
        model_width = 2.0f * aspect;
    }
    float scale_x = model_width / (float)sw;
    float scale_y = model_height / (float)sh;
    // Depth scale proportional to the smaller dimension for natural look
    float depth_scale = std::min(model_width, model_height) * 0.4f;

    std::vector<int> grid(sw * sh, -1);

    for (int gy = 0; gy < sh; gy++) {
        for (int gx = 0; gx < sw; gx++) {
            int px = gx * downsample + downsample / 2;
            int py = gy * downsample + downsample / 2;
            px = std::clamp(px, 0, w - 1);
            py = std::clamp(py, 0, h - 1);

            if (!mask[py * w + px]) continue;

            float d_sum = 0; int d_count = 0;
            for (int dy = 0; dy < downsample; dy++) {
                for (int dx = 0; dx < downsample; dx++) {
                    int sx2 = gx * downsample + dx;
                    int sy2 = gy * downsample + dy;
                    if (sx2 < w && sy2 < h) {
                        d_sum += depth[sy2 * w + sx2];
                        d_count++;
                    }
                }
            }
            float d = d_count > 0 ? d_sum / d_count : 0.5f;

            Vertex v;
            v.pos[0] = (gx - sw * 0.5f) * scale_x;
            v.pos[1] = (sh - gy - sh * 0.5f) * scale_y; // center vertically
            v.pos[2] = d * depth_scale;
            v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1;

            int cidx = (py * w + px) * 4;
            v.color[0] = img.pixels[cidx + 0] / 255.0f;
            v.color[1] = img.pixels[cidx + 1] / 255.0f;
            v.color[2] = img.pixels[cidx + 2] / 255.0f;

            grid[gy * sw + gx] = (int)mesh.vertices.size();
            mesh.vertices.push_back(v);
        }
    }

    // Front face triangles — only create quads where all 4 corners exist
    for (int gy = 0; gy < sh - 1; gy++) {
        for (int gx = 0; gx < sw - 1; gx++) {
            int i00 = grid[gy * sw + gx];
            int i10 = grid[gy * sw + gx + 1];
            int i01 = grid[(gy + 1) * sw + gx];
            int i11 = grid[(gy + 1) * sw + gx + 1];

            // Prefer full quads for cleaner mesh
            if (i00 >= 0 && i10 >= 0 && i01 >= 0 && i11 >= 0) {
                mesh.indices.push_back(i00); mesh.indices.push_back(i01); mesh.indices.push_back(i10);
                mesh.indices.push_back(i10); mesh.indices.push_back(i01); mesh.indices.push_back(i11);
            } else if (i00 >= 0 && i10 >= 0 && i01 >= 0) {
                mesh.indices.push_back(i00); mesh.indices.push_back(i01); mesh.indices.push_back(i10);
            } else if (i10 >= 0 && i01 >= 0 && i11 >= 0) {
                mesh.indices.push_back(i10); mesh.indices.push_back(i01); mesh.indices.push_back(i11);
            }
        }
    }

    int front_vert_count = (int)mesh.vertices.size();
    int front_idx_count = (int)mesh.indices.size();

    // Back face — mirror Z, flatten to a slight offset behind
    // Find the min depth value to use as the back plane
    float min_z = 1e30f;
    for (int i = 0; i < front_vert_count; i++) {
        min_z = std::min(min_z, mesh.vertices[i].pos[2]);
    }
    float back_z = min_z - 0.05f; // small offset behind the shallowest point

    for (int i = 0; i < front_vert_count; i++) {
        Vertex v = mesh.vertices[i];
        v.pos[2] = back_z; // flat back plane
        v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = -1;
        v.color[0] *= 0.7f; v.color[1] *= 0.7f; v.color[2] *= 0.7f;
        mesh.vertices.push_back(v);
    }
    // Back face triangles (reversed winding)
    for (int i = 0; i < front_idx_count; i += 3) {
        mesh.indices.push_back(mesh.indices[i + 0] + front_vert_count);
        mesh.indices.push_back(mesh.indices[i + 2] + front_vert_count);
        mesh.indices.push_back(mesh.indices[i + 1] + front_vert_count);
    }

    // Find boundary edges and stitch front to back
    // A boundary edge is an edge that belongs to exactly 1 triangle
    log("Stitching front and back faces...", 0);
    {
        // Build edge->triangle count map (only for front face triangles)
        struct EdgeKey {
            unsigned int a, b;
            bool operator<(const EdgeKey& o) const {
                return std::tie(a, b) < std::tie(o.a, o.b);
            }
        };
        auto make_edge = [](unsigned int v0, unsigned int v1) -> EdgeKey {
            return v0 < v1 ? EdgeKey{v0, v1} : EdgeKey{v1, v0};
        };

        std::map<EdgeKey, int> edge_count;
        // Also store directed boundary edges to get correct winding
        std::map<EdgeKey, std::pair<unsigned int, unsigned int>> edge_directed;

        for (int i = 0; i < front_idx_count; i += 3) {
            for (int e = 0; e < 3; e++) {
                unsigned int v0 = mesh.indices[i + e];
                unsigned int v1 = mesh.indices[i + (e + 1) % 3];
                EdgeKey ek = make_edge(v0, v1);
                edge_count[ek]++;
                edge_directed[ek] = {v0, v1}; // last wins, gives us one direction
            }
        }

        int stitched = 0;
        for (auto& [ek, count] : edge_count) {
            if (count != 1) continue; // not a boundary edge

            // Get the directed edge (as it appears in the front face)
            auto [v0, v1] = edge_directed[ek];
            unsigned int v0_back = v0 + front_vert_count;
            unsigned int v1_back = v1 + front_vert_count;

            // Create a quad (2 triangles) connecting front edge to back edge
            // Winding: front edge goes v0->v1, so side wall goes v0->v0_back->v1_back->v1
            mesh.indices.push_back(v0);
            mesh.indices.push_back(v0_back);
            mesh.indices.push_back(v1);

            mesh.indices.push_back(v1);
            mesh.indices.push_back(v0_back);
            mesh.indices.push_back(v1_back);

            stitched++;
        }
        log("Stitched " + std::to_string(stitched) + " boundary edges", 1);
    }

    // Laplacian smoothing to reduce jagged edges
    log("Smoothing mesh (Laplacian, 5 iterations)...", 0);
    laplacian_smooth(mesh, 5, 0.5f);

    log("Depth mesh created: " + std::to_string(mesh.vertices.size()) + " vertices, " +
        std::to_string(mesh.indices.size() / 3) + " triangles", 1);
    return mesh;
}
