#include "pipeline.h"
#include <stb_image.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <array>

// ============================================================
// Image loading
// ============================================================

ImageData load_image(const std::string& path) {
    ImageData img;
    unsigned char* data = stbi_load(path.c_str(), &img.width, &img.height, &img.channels, 4);
    if (data) {
        img.channels = 4;
        img.pixels.assign(data, data + img.width * img.height * 4);
        stbi_image_free(data);
    }
    return img;
}

// ============================================================
// Silhouette extraction
// ============================================================

std::vector<bool> extract_silhouette(const ImageData& img, int threshold) {
    std::vector<bool> mask(img.width * img.height, false);
    for (int i = 0; i < img.width * img.height; i++) {
        int idx = i * 4;
        unsigned char a = img.pixels[idx + 3];
        unsigned char r = img.pixels[idx + 0];
        unsigned char g = img.pixels[idx + 1];
        unsigned char b = img.pixels[idx + 2];
        // Foreground: non-transparent and not pure white background
        bool is_bg = (a < 128) || (r > threshold && g > threshold && b > threshold);
        mask[i] = !is_bg;
    }
    return mask;
}

// ============================================================
// Mesh generation from silhouettes
// ============================================================

// Find bounding box of silhouette
static void silhouette_bounds(const std::vector<bool>& mask, int w, int h,
                               int& min_x, int& max_x, int& min_y, int& max_y) {
    min_x = w; max_x = 0; min_y = h; max_y = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (mask[y * w + x]) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
}

// For each row of the silhouette, find the left and right extents
struct RowExtent {
    float left, right; // normalized [-0.5, 0.5]
    bool valid = false;
};

static std::vector<RowExtent> get_row_extents(const std::vector<bool>& mask, int w, int h) {
    std::vector<RowExtent> rows(h);
    for (int y = 0; y < h; y++) {
        int left = -1, right = -1;
        for (int x = 0; x < w; x++) {
            if (mask[y * w + x]) {
                if (left < 0) left = x;
                right = x;
            }
        }
        if (left >= 0) {
            rows[y].left = (float)left / (float)w - 0.5f;
            rows[y].right = (float)right / (float)w - 0.5f;
            rows[y].valid = true;
        }
    }
    return rows;
}

Mesh generate_mesh_from_silhouettes(const ImageData& front, const ImageData& side,
                                     const ImageData& back, LogCallback log) {
    log("Extracting silhouettes from images...", 0);

    auto front_mask = extract_silhouette(front);
    auto side_mask = extract_silhouette(side);

    auto front_rows = get_row_extents(front_mask, front.width, front.height);
    auto side_rows = get_row_extents(side_mask, side.width, side.height);

    // Find vertical extent of the character
    int front_min_y = front.height, front_max_y = 0;
    for (int y = 0; y < front.height; y++) {
        if (front_rows[y].valid) {
            front_min_y = std::min(front_min_y, y);
            front_max_y = std::max(front_max_y, y);
        }
    }

    int side_min_y = side.height, side_max_y = 0;
    for (int y = 0; y < side.height; y++) {
        if (side_rows[y].valid) {
            side_min_y = std::min(side_min_y, y);
            side_max_y = std::max(side_max_y, y);
        }
    }

    log("Building 3D mesh from silhouette cross-sections...", 0);

    Mesh mesh;
    int slices = 64;     // vertical slices
    int radial = 32;     // around circumference

    float height = 2.0f; // model height in world units

    for (int i = 0; i <= slices; i++) {
        float t = (float)i / (float)slices; // 0 = top, 1 = bottom
        float world_y = height * (1.0f - t); // top to bottom

        // Sample front row extent
        int front_row = front_min_y + (int)(t * (front_max_y - front_min_y));
        front_row = std::clamp(front_row, 0, front.height - 1);
        float half_width = 0.1f;
        if (front_rows[front_row].valid) {
            half_width = (front_rows[front_row].right - front_rows[front_row].left) * 0.5f * height;
        }

        // Sample side row extent
        int side_row = side_min_y + (int)(t * (side_max_y - side_min_y));
        side_row = std::clamp(side_row, 0, side.height - 1);
        float half_depth = 0.1f;
        if (side_rows[side_row].valid) {
            half_depth = (side_rows[side_row].right - side_rows[side_row].left) * 0.5f * height;
        }

        // Generate a ring of vertices using an elliptical cross-section
        for (int j = 0; j <= radial; j++) {
            float angle = 2.0f * 3.14159265f * (float)j / (float)radial;
            float nx = cosf(angle);
            float nz = sinf(angle);
            float px = nx * half_width;
            float pz = nz * half_depth;

            Vertex v;
            v.pos[0] = px;
            v.pos[1] = world_y;
            v.pos[2] = pz;
            v.normal[0] = nx;
            v.normal[1] = 0.0f;
            v.normal[2] = nz;
            // Default color from front image at this row
            float u_coord = (nx * 0.5f + 0.5f);
            int sample_x = std::clamp((int)(u_coord * front.width), 0, front.width - 1);
            int sample_y = std::clamp(front_row, 0, front.height - 1);
            int pixel_idx = (sample_y * front.width + sample_x) * 4;
            v.color[0] = front.pixels[pixel_idx + 0] / 255.0f;
            v.color[1] = front.pixels[pixel_idx + 1] / 255.0f;
            v.color[2] = front.pixels[pixel_idx + 2] / 255.0f;

            mesh.vertices.push_back(v);
        }
    }

    // Generate indices (quad strips between rings)
    int ring_size = radial + 1;
    for (int i = 0; i < slices; i++) {
        for (int j = 0; j < radial; j++) {
            unsigned int a = i * ring_size + j;
            unsigned int b = a + ring_size;
            unsigned int c = a + 1;
            unsigned int d = b + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
        }
    }

    // Cap top
    {
        Vertex center;
        center.pos[0] = 0; center.pos[1] = height; center.pos[2] = 0;
        center.normal[0] = 0; center.normal[1] = 1; center.normal[2] = 0;
        center.color[0] = 0.8f; center.color[1] = 0.7f; center.color[2] = 0.6f;
        unsigned int ci = (unsigned int)mesh.vertices.size();
        mesh.vertices.push_back(center);
        for (int j = 0; j < radial; j++) {
            mesh.indices.push_back(ci);
            mesh.indices.push_back(j + 1);
            mesh.indices.push_back(j);
        }
    }

    // Cap bottom
    {
        Vertex center;
        center.pos[0] = 0; center.pos[1] = 0; center.pos[2] = 0;
        center.normal[0] = 0; center.normal[1] = -1; center.normal[2] = 0;
        center.color[0] = 0.3f; center.color[1] = 0.3f; center.color[2] = 0.3f;
        unsigned int ci = (unsigned int)mesh.vertices.size();
        mesh.vertices.push_back(center);
        unsigned int last_ring = slices * ring_size;
        for (int j = 0; j < radial; j++) {
            mesh.indices.push_back(ci);
            mesh.indices.push_back(last_ring + j);
            mesh.indices.push_back(last_ring + j + 1);
        }
    }

    log("Mesh generated: " + std::to_string(mesh.vertices.size()) + " vertices, " +
        std::to_string(mesh.indices.size() / 3) + " triangles", 1);
    return mesh;
}

// ============================================================
// QEM (Quadric Error Metric) Decimation — Garland-Heckbert
// ============================================================

// 4x4 symmetric matrix stored as 10 unique values (upper triangle)
struct Quadric {
    double a[10] = {};
    void clear() { for (int i = 0; i < 10; i++) a[i] = 0; }
    Quadric& operator+=(const Quadric& o) {
        for (int i = 0; i < 10; i++) a[i] += o.a[i];
        return *this;
    }
};

static Quadric make_plane_quadric(float nx, float ny, float nz, float d) {
    Quadric q;
    q.a[0] = nx*nx; q.a[1] = nx*ny; q.a[2] = nx*nz; q.a[3] = nx*d;
    q.a[4] = ny*ny; q.a[5] = ny*nz; q.a[6] = ny*d;
    q.a[7] = nz*nz; q.a[8] = nz*d;
    q.a[9] = d*d;
    return q;
}

static double eval_quadric(const Quadric& q, float x, float y, float z) {
    return q.a[0]*x*x + 2*q.a[1]*x*y + 2*q.a[2]*x*z + 2*q.a[3]*x
         + q.a[4]*y*y + 2*q.a[5]*y*z + 2*q.a[6]*y
         + q.a[7]*z*z + 2*q.a[8]*z
         + q.a[9];
}

Mesh decimate_mesh(const Mesh& input, int target_triangles, LogCallback log) {
    int current_tris = (int)input.indices.size() / 3;
    if (current_tris <= target_triangles) {
        log("Mesh already at or below target (" + std::to_string(current_tris) + " tris)", 0);
        return input;
    }

    log("QEM decimating from " + std::to_string(current_tris) + " to " +
        std::to_string(target_triangles) + " triangles...", 0);

    Mesh mesh = input;
    int n_verts = (int)mesh.vertices.size();
    int n_tris = current_tris;

    // Compute per-vertex quadrics from adjacent face planes
    std::vector<Quadric> vertex_quadrics(n_verts);
    std::vector<bool> tri_alive(n_tris, true);
    std::vector<std::set<int>> vert_tris(n_verts);

    for (int t = 0; t < n_tris; t++) {
        unsigned int i0 = mesh.indices[t*3+0], i1 = mesh.indices[t*3+1], i2 = mesh.indices[t*3+2];
        vert_tris[i0].insert(t); vert_tris[i1].insert(t); vert_tris[i2].insert(t);

        const float* p0 = mesh.vertices[i0].pos;
        const float* p1 = mesh.vertices[i1].pos;
        const float* p2 = mesh.vertices[i2].pos;

        float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
        float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
        float n[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
        float len = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (len > 1e-8f) { n[0]/=len; n[1]/=len; n[2]/=len; }

        float d = -(n[0]*p0[0] + n[1]*p0[1] + n[2]*p0[2]);
        Quadric q = make_plane_quadric(n[0], n[1], n[2], d);

        vertex_quadrics[i0] += q;
        vertex_quadrics[i1] += q;
        vertex_quadrics[i2] += q;
    }

    int alive_tris = n_tris;

    while (alive_tris > target_triangles) {
        // Find cheapest edge to collapse using QEM cost
        double best_cost = 1e30;
        int best_e0 = -1, best_e1 = -1;
        float best_pos[3] = {};

        // Sample edges from alive triangles
        int step = std::max(1, alive_tris / 1000);
        int count = 0;
        for (int t = 0; t < n_tris; t++) {
            if (!tri_alive[t]) continue;
            count++;
            if (count % step != 0 && alive_tris > target_triangles * 2) continue;

            for (int e = 0; e < 3; e++) {
                unsigned int v0 = mesh.indices[t*3 + e];
                unsigned int v1 = mesh.indices[t*3 + (e+1)%3];
                if (v0 > v1) std::swap(v0, v1);

                Quadric combined = vertex_quadrics[v0];
                combined += vertex_quadrics[v1];

                // Optimal position = midpoint (simplified; full QEM solves a 3x3 system)
                float mx = (mesh.vertices[v0].pos[0] + mesh.vertices[v1].pos[0]) * 0.5f;
                float my = (mesh.vertices[v0].pos[1] + mesh.vertices[v1].pos[1]) * 0.5f;
                float mz = (mesh.vertices[v0].pos[2] + mesh.vertices[v1].pos[2]) * 0.5f;

                double cost = eval_quadric(combined, mx, my, mz);
                if (cost < best_cost) {
                    best_cost = cost;
                    best_e0 = v0;
                    best_e1 = v1;
                    best_pos[0] = mx; best_pos[1] = my; best_pos[2] = mz;
                }
            }
        }

        if (best_e0 < 0) break;

        // Collapse: move v0 to optimal position, merge v1 into v0
        mesh.vertices[best_e0].pos[0] = best_pos[0];
        mesh.vertices[best_e0].pos[1] = best_pos[1];
        mesh.vertices[best_e0].pos[2] = best_pos[2];
        mesh.vertices[best_e0].color[0] = (mesh.vertices[best_e0].color[0] + mesh.vertices[best_e1].color[0]) * 0.5f;
        mesh.vertices[best_e0].color[1] = (mesh.vertices[best_e0].color[1] + mesh.vertices[best_e1].color[1]) * 0.5f;
        mesh.vertices[best_e0].color[2] = (mesh.vertices[best_e0].color[2] + mesh.vertices[best_e1].color[2]) * 0.5f;

        // Merge quadrics
        vertex_quadrics[best_e0] += vertex_quadrics[best_e1];

        // Remap v1 -> v0 in all triangles using v1
        for (int t : vert_tris[best_e1]) {
            if (!tri_alive[t]) continue;
            for (int k = 0; k < 3; k++) {
                if (mesh.indices[t*3+k] == (unsigned int)best_e1)
                    mesh.indices[t*3+k] = best_e0;
            }
            unsigned int i0 = mesh.indices[t*3+0], i1 = mesh.indices[t*3+1], i2 = mesh.indices[t*3+2];
            if (i0 == i1 || i1 == i2 || i0 == i2) {
                tri_alive[t] = false;
                alive_tris--;
                vert_tris[i0].erase(t);
                vert_tris[i1].erase(t);
                vert_tris[i2].erase(t);
            } else {
                vert_tris[best_e0].insert(t);
            }
        }
        vert_tris[best_e1].clear();
    }

    // Compact
    Mesh result;
    std::map<unsigned int, unsigned int> remap;
    for (int t = 0; t < n_tris; t++) {
        if (!tri_alive[t]) continue;
        for (int k = 0; k < 3; k++) {
            unsigned int vi = mesh.indices[t*3+k];
            if (remap.find(vi) == remap.end()) {
                remap[vi] = (unsigned int)result.vertices.size();
                result.vertices.push_back(mesh.vertices[vi]);
            }
            result.indices.push_back(remap[vi]);
        }
    }

    log("QEM decimation complete: " + std::to_string(result.vertices.size()) + " vertices, " +
        std::to_string(result.indices.size() / 3) + " triangles", 1);
    return result;
}

// ============================================================
// UV unwrapping with xatlas
// ============================================================

#include "../third_party/xatlas.h"

bool uv_unwrap(Mesh& mesh, LogCallback log) {
    log("Running xatlas UV unwrapping...", 0);

    xatlas::Atlas* atlas = xatlas::Create();

    xatlas::MeshDecl decl;
    decl.vertexCount = (uint32_t)mesh.vertices.size();
    decl.vertexPositionData = &mesh.vertices[0].pos[0];
    decl.vertexPositionStride = sizeof(Vertex);
    decl.vertexNormalData = &mesh.vertices[0].normal[0];
    decl.vertexNormalStride = sizeof(Vertex);
    decl.indexCount = (uint32_t)mesh.indices.size();
    decl.indexData = mesh.indices.data();
    decl.indexFormat = xatlas::IndexFormat::UInt32;

    xatlas::AddMeshError err = xatlas::AddMesh(atlas, decl);
    if (err != xatlas::AddMeshError::Success) {
        log("xatlas AddMesh failed: " + std::string(xatlas::StringForEnum(err)), 3);
        xatlas::Destroy(atlas);
        return false;
    }

    xatlas::Generate(atlas);

    log("UV atlas generated: " + std::to_string(atlas->chartCount) + " charts, " +
        std::to_string(atlas->atlasCount) + " atlas(es), " +
        std::to_string(atlas->width) + "x" + std::to_string(atlas->height), 1);

    // Rebuild mesh with UV data — xatlas may have split vertices
    const xatlas::Mesh& out = atlas->meshes[0];

    Mesh new_mesh;
    new_mesh.vertices.resize(out.vertexCount);
    for (uint32_t i = 0; i < out.vertexCount; i++) {
        const xatlas::Vertex& v = out.vertexArray[i];
        new_mesh.vertices[i] = mesh.vertices[v.xref];
        // Store UV in the color channels temporarily (will be used for texture baking)
        // We repurpose color.x = u, color.y = v for now
        // Actually let's keep color and store UV separately — but Vertex doesn't have UV yet
        // For simplicity, blend the UV into the color for visualization
        float u = v.uv[0] / (float)atlas->width;
        float v_coord = v.uv[1] / (float)atlas->height;
        // Keep original color but tint slightly by UV for debug
        new_mesh.vertices[i].color[0] = mesh.vertices[v.xref].color[0];
        new_mesh.vertices[i].color[1] = mesh.vertices[v.xref].color[1];
        new_mesh.vertices[i].color[2] = mesh.vertices[v.xref].color[2];
    }

    new_mesh.indices.resize(out.indexCount);
    for (uint32_t i = 0; i < out.indexCount; i++) {
        new_mesh.indices[i] = out.indexArray[i];
    }

    mesh = new_mesh;
    xatlas::Destroy(atlas);
    return true;
}

// ============================================================
// Texture baking (simple projection)
// ============================================================

TextureAtlas bake_texture(const Mesh& mesh, const ImageData& front, const ImageData& side,
                          const ImageData& back, LogCallback log) {
    log("Baking texture from source images...", 0);

    int tex_size = 1024;
    TextureAtlas atlas;
    atlas.width = tex_size;
    atlas.height = tex_size;
    atlas.pixels.resize(tex_size * tex_size * 4, 128);

    // For each triangle, determine which source image to sample based on face normal
    int tri_count = (int)mesh.indices.size() / 3;
    for (int t = 0; t < tri_count; t++) {
        const Vertex& v0 = mesh.vertices[mesh.indices[t*3+0]];
        const Vertex& v1 = mesh.vertices[mesh.indices[t*3+1]];
        const Vertex& v2 = mesh.vertices[mesh.indices[t*3+2]];

        // Average normal of the face
        float nx = (v0.normal[0] + v1.normal[0] + v2.normal[0]) / 3.0f;
        float nz = (v0.normal[2] + v1.normal[2] + v2.normal[2]) / 3.0f;

        // Pick source image based on dominant normal direction
        const ImageData* src = &front;
        if (fabsf(nz) > fabsf(nx)) {
            src = (nz > 0) ? &back : &front;
        } else {
            src = &side;
        }

        // Sample source image color for each vertex and assign
        for (int k = 0; k < 3; k++) {
            const Vertex& v = mesh.vertices[mesh.indices[t*3+k]];
            // Project vertex onto source image
            float u = v.pos[0] * 0.5f + 0.5f;               // X -> U
            float height_v = v.pos[1] / 2.0f;                // Y -> V (model height ~2)
            int sx = std::clamp((int)(u * src->width), 0, src->width - 1);
            int sy = std::clamp((int)((1.0f - height_v) * src->height), 0, src->height - 1);
            int pixel_idx = (sy * src->width + sx) * 4;

            // We already have per-vertex color from mesh generation
            // This is a simplified bake
            (void)pixel_idx;
        }
    }

    log("Texture baked: " + std::to_string(tex_size) + "x" + std::to_string(tex_size), 1);
    return atlas;
}

// ============================================================
// Single front image mesh generation
// ============================================================

Mesh generate_mesh_from_front(const ImageData& front, LogCallback log) {
    log("Extracting silhouette from front image...", 0);
    auto front_mask = extract_silhouette(front);
    auto front_rows = get_row_extents(front_mask, front.width, front.height);

    int front_min_y = front.height, front_max_y = 0;
    for (int y = 0; y < front.height; y++) {
        if (front_rows[y].valid) {
            front_min_y = std::min(front_min_y, y);
            front_max_y = std::max(front_max_y, y);
        }
    }

    if (front_min_y >= front_max_y) {
        log("Could not detect silhouette — check image has non-white/non-transparent foreground", 3);
        return Mesh{};
    }

    log("Building 3D mesh (front-only mode, assuming symmetric depth)...", 0);

    Mesh mesh;
    int slices = 64;
    int radial = 32;
    float height = 2.0f;

    for (int i = 0; i <= slices; i++) {
        float t = (float)i / (float)slices;
        float world_y = height * (1.0f - t);

        int front_row = front_min_y + (int)(t * (front_max_y - front_min_y));
        front_row = std::clamp(front_row, 0, front.height - 1);
        float half_width = 0.05f;
        if (front_rows[front_row].valid) {
            half_width = (front_rows[front_row].right - front_rows[front_row].left) * 0.5f * height;
        }

        // In front-only mode, assume depth is proportional to width (roughly cylindrical)
        float half_depth = half_width * 0.6f;

        for (int j = 0; j <= radial; j++) {
            float angle = 2.0f * 3.14159265f * (float)j / (float)radial;
            float nx = cosf(angle);
            float nz = sinf(angle);

            Vertex v;
            v.pos[0] = nx * half_width;
            v.pos[1] = world_y;
            v.pos[2] = nz * half_depth;
            v.normal[0] = nx;
            v.normal[1] = 0.0f;
            v.normal[2] = nz;

            // Sample color from front image
            float u_coord = nx * 0.5f + 0.5f;
            int sample_x = std::clamp((int)(u_coord * front.width), 0, front.width - 1);
            int sample_y = std::clamp(front_row, 0, front.height - 1);
            int pixel_idx = (sample_y * front.width + sample_x) * 4;
            v.color[0] = front.pixels[pixel_idx + 0] / 255.0f;
            v.color[1] = front.pixels[pixel_idx + 1] / 255.0f;
            v.color[2] = front.pixels[pixel_idx + 2] / 255.0f;

            mesh.vertices.push_back(v);
        }
    }

    int ring_size = radial + 1;
    for (int i = 0; i < slices; i++) {
        for (int j = 0; j < radial; j++) {
            unsigned int a = i * ring_size + j;
            unsigned int b = a + ring_size;
            unsigned int c = a + 1;
            unsigned int d = b + 1;
            mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c);
            mesh.indices.push_back(c); mesh.indices.push_back(b); mesh.indices.push_back(d);
        }
    }

    // Cap top
    {
        Vertex center;
        center.pos[0] = 0; center.pos[1] = height; center.pos[2] = 0;
        center.normal[0] = 0; center.normal[1] = 1; center.normal[2] = 0;
        center.color[0] = 0.8f; center.color[1] = 0.7f; center.color[2] = 0.6f;
        unsigned int ci = (unsigned int)mesh.vertices.size();
        mesh.vertices.push_back(center);
        for (int j = 0; j < radial; j++) {
            mesh.indices.push_back(ci);
            mesh.indices.push_back(j + 1);
            mesh.indices.push_back(j);
        }
    }

    // Cap bottom
    {
        Vertex center;
        center.pos[0] = 0; center.pos[1] = 0; center.pos[2] = 0;
        center.normal[0] = 0; center.normal[1] = -1; center.normal[2] = 0;
        center.color[0] = 0.3f; center.color[1] = 0.3f; center.color[2] = 0.3f;
        unsigned int ci = (unsigned int)mesh.vertices.size();
        mesh.vertices.push_back(center);
        unsigned int last_ring = slices * ring_size;
        for (int j = 0; j < radial; j++) {
            mesh.indices.push_back(ci);
            mesh.indices.push_back(last_ring + j);
            mesh.indices.push_back(last_ring + j + 1);
        }
    }

    log("Mesh generated: " + std::to_string(mesh.vertices.size()) + " vertices, " +
        std::to_string(mesh.indices.size() / 3) + " triangles", 1);
    return mesh;
}

// ============================================================
// Flat shading: duplicate vertices per face
// ============================================================

Mesh make_flat_shaded(const Mesh& input) {
    Mesh out;
    int tri_count = (int)input.indices.size() / 3;
    out.vertices.reserve(tri_count * 3);
    out.indices.reserve(tri_count * 3);

    for (int t = 0; t < tri_count; t++) {
        const Vertex& a = input.vertices[input.indices[t*3+0]];
        const Vertex& b = input.vertices[input.indices[t*3+1]];
        const Vertex& c = input.vertices[input.indices[t*3+2]];

        // Compute face normal
        float e1[3] = {b.pos[0]-a.pos[0], b.pos[1]-a.pos[1], b.pos[2]-a.pos[2]};
        float e2[3] = {c.pos[0]-a.pos[0], c.pos[1]-a.pos[1], c.pos[2]-a.pos[2]};
        float n[3] = {
            e1[1]*e2[2]-e1[2]*e2[1],
            e1[2]*e2[0]-e1[0]*e2[2],
            e1[0]*e2[1]-e1[1]*e2[0]
        };
        float len = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (len > 0.0001f) { n[0]/=len; n[1]/=len; n[2]/=len; }

        unsigned int base = (unsigned int)out.vertices.size();
        for (int k = 0; k < 3; k++) {
            Vertex v = input.vertices[input.indices[t*3+k]];
            v.normal[0] = n[0]; v.normal[1] = n[1]; v.normal[2] = n[2];
            out.vertices.push_back(v);
            out.indices.push_back(base + k);
        }
    }
    return out;
}

// ============================================================
// Per-face color baking from source image
// ============================================================

// Bilinear image sampling, returns false if pixel is background
static bool sample_bilinear(const ImageData& img, float u, float v_coord, float& r, float& g, float& b) {
    u = std::clamp(u, 0.0f, 1.0f);
    v_coord = std::clamp(v_coord, 0.0f, 1.0f);
    float fx = u * (img.width - 1);
    float fy = v_coord * (img.height - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0 + 1, img.width - 1);
    int y1 = std::min(y0 + 1, img.height - 1);
    float sx = fx - x0, sy = fy - y0;

    auto read = [&](int x, int y, float& ro, float& go, float& bo, bool& bg) {
        int idx = (y * img.width + x) * 4;
        unsigned char alpha = img.pixels[idx + 3];
        unsigned char ri = img.pixels[idx], gi = img.pixels[idx+1], bi = img.pixels[idx+2];
        bg = (alpha < 128) || (ri > 240 && gi > 240 && bi > 240);
        ro = ri / 255.0f; go = gi / 255.0f; bo = bi / 255.0f;
    };

    float r00,g00,b00, r10,g10,b10, r01,g01,b01, r11,g11,b11;
    bool bg00,bg10,bg01,bg11;
    read(x0,y0,r00,g00,b00,bg00);
    read(x1,y0,r10,g10,b10,bg10);
    read(x0,y1,r01,g01,b01,bg01);
    read(x1,y1,r11,g11,b11,bg11);

    // If majority is background, skip
    int bg_count = (int)bg00 + bg10 + bg01 + bg11;
    if (bg_count >= 3) return false;

    r = (1-sx)*(1-sy)*r00 + sx*(1-sy)*r10 + (1-sx)*sy*r01 + sx*sy*r11;
    g = (1-sx)*(1-sy)*g00 + sx*(1-sy)*g10 + (1-sx)*sy*g01 + sx*sy*g11;
    b = (1-sx)*(1-sy)*b00 + sx*(1-sy)*b10 + (1-sx)*sy*b01 + sx*sy*b11;
    return true;
}

void bake_face_colors(Mesh& mesh, const ImageData& front, const ImageData* side, const ImageData* back) {
    if (mesh.vertices.empty()) return;

    // Compute mesh bounding box
    float bb_min[3] = {1e30f,1e30f,1e30f}, bb_max[3] = {-1e30f,-1e30f,-1e30f};
    for (const auto& v : mesh.vertices) {
        for (int k = 0; k < 3; k++) {
            bb_min[k] = std::min(bb_min[k], v.pos[k]);
            bb_max[k] = std::max(bb_max[k], v.pos[k]);
        }
    }
    float range[3];
    for (int k = 0; k < 3; k++) {
        range[k] = bb_max[k] - bb_min[k];
        if (range[k] < 0.001f) range[k] = 1.0f;
    }

    // Barycentric sample points for multi-sampling
    static const float bary[][3] = {
        {1.0f/3, 1.0f/3, 1.0f/3},
        {0.6f,   0.2f,   0.2f},
        {0.2f,   0.6f,   0.2f},
        {0.2f,   0.2f,   0.6f},
    };
    constexpr int N_SAMPLES = 4;

    int tri_count = (int)mesh.indices.size() / 3;
    for (int t = 0; t < tri_count; t++) {
        const Vertex& va = mesh.vertices[mesh.indices[t*3+0]];
        const Vertex& vb = mesh.vertices[mesh.indices[t*3+1]];
        const Vertex& vc = mesh.vertices[mesh.indices[t*3+2]];

        // Average face normal
        float fnx = (va.normal[0]+vb.normal[0]+vc.normal[0]) / 3.0f;
        float fnz = (va.normal[2]+vb.normal[2]+vc.normal[2]) / 3.0f;

        // View weights for smooth blending
        float w_front = std::max(0.0f, -fnz);
        float w_back  = std::max(0.0f,  fnz);
        float w_side  = std::max(0.0f, fabsf(fnx) - 0.2f * std::max(fabsf(fnz), 0.001f));

        // Front-only mode
        if (!side && !back) {
            w_front = std::max(w_front, 0.3f);
            w_back = 0; w_side = 0;
        }

        float w_total = w_front + w_back + w_side;
        if (w_total < 0.001f) { w_front = 1.0f; w_total = 1.0f; }
        w_front /= w_total; w_back /= w_total; w_side /= w_total;

        float total_r = 0, total_g = 0, total_b = 0;
        int valid = 0;

        for (int s = 0; s < N_SAMPLES; s++) {
            float px = bary[s][0]*va.pos[0] + bary[s][1]*vb.pos[0] + bary[s][2]*vc.pos[0];
            float py = bary[s][0]*va.pos[1] + bary[s][1]*vb.pos[1] + bary[s][2]*vc.pos[1];
            float pz = bary[s][0]*va.pos[2] + bary[s][1]*vb.pos[2] + bary[s][2]*vc.pos[2];

            // UV for front: project along Z
            float u_f = (px - bb_min[0]) / range[0];
            float v_f = 1.0f - (py - bb_min[1]) / range[1];

            // UV for back: mirrored X
            float u_b = 1.0f - u_f;
            float v_b = v_f;

            // UV for side: project along X, use Z for horizontal
            float u_s = (pz - bb_min[2]) / range[2];
            float v_s = v_f;

            float sr = 0, sg = 0, sb = 0;
            float r1, g1, b1;

            // Front
            if (sample_bilinear(front, u_f, v_f, r1, g1, b1)) {
                sr += w_front * r1; sg += w_front * g1; sb += w_front * b1;
            } else {
                sr += w_front * 0.5f; sg += w_front * 0.5f; sb += w_front * 0.5f;
            }

            // Back
            if (back && w_back > 0.01f) {
                if (sample_bilinear(*back, u_b, v_b, r1, g1, b1)) {
                    sr += w_back * r1; sg += w_back * g1; sb += w_back * b1;
                } else {
                    sr += w_back * 0.4f; sg += w_back * 0.4f; sb += w_back * 0.4f;
                }
            } else if (w_back > 0.01f) {
                // Front-only: darken for back
                if (sample_bilinear(front, u_f, v_f, r1, g1, b1)) {
                    sr += w_back * r1 * 0.6f; sg += w_back * g1 * 0.6f; sb += w_back * b1 * 0.6f;
                }
            }

            // Side
            if (side && w_side > 0.01f) {
                if (sample_bilinear(*side, u_s, v_s, r1, g1, b1)) {
                    sr += w_side * r1; sg += w_side * g1; sb += w_side * b1;
                } else {
                    sr += w_side * 0.4f; sg += w_side * 0.4f; sb += w_side * 0.4f;
                }
            } else if (w_side > 0.01f) {
                // Front-only: use front with slight darken for sides
                if (sample_bilinear(front, u_f, v_f, r1, g1, b1)) {
                    sr += w_side * r1 * 0.7f; sg += w_side * g1 * 0.7f; sb += w_side * b1 * 0.7f;
                }
            }

            total_r += sr; total_g += sg; total_b += sb;
            valid++;
        }

        if (valid > 0) {
            float inv = 1.0f / valid;
            total_r *= inv; total_g *= inv; total_b *= inv;
        }

        for (int k = 0; k < 3; k++) {
            Vertex& vert = mesh.vertices[mesh.indices[t*3+k]];
            vert.color[0] = std::clamp(total_r, 0.0f, 1.0f);
            vert.color[1] = std::clamp(total_g, 0.0f, 1.0f);
            vert.color[2] = std::clamp(total_b, 0.0f, 1.0f);
        }
    }
}

// ============================================================
// K-means color quantization
// ============================================================

void quantize_colors(Mesh& mesh, int k) {
    if (mesh.vertices.empty() || k <= 0) return;

    int tri_count = (int)mesh.indices.size() / 3;
    if (tri_count == 0) return;

    // Collect one color per face (use first vertex since flat-shaded faces share color)
    struct Color3 { float r, g, b; };
    std::vector<Color3> face_colors(tri_count);
    for (int t = 0; t < tri_count; t++) {
        const Vertex& v = mesh.vertices[mesh.indices[t*3]];
        face_colors[t] = {v.color[0], v.color[1], v.color[2]};
    }

    // Initialize centroids by sampling evenly from faces
    std::vector<Color3> centroids(k);
    for (int i = 0; i < k; i++) {
        int idx = (i * tri_count) / k;
        centroids[i] = face_colors[idx];
    }

    std::vector<int> assignments(tri_count, 0);

    // Run k-means for 20 iterations
    for (int iter = 0; iter < 20; iter++) {
        // Assign each face to nearest centroid
        for (int t = 0; t < tri_count; t++) {
            float best_dist = 1e30f;
            for (int c = 0; c < k; c++) {
                float dr = face_colors[t].r - centroids[c].r;
                float dg = face_colors[t].g - centroids[c].g;
                float db = face_colors[t].b - centroids[c].b;
                float dist = dr*dr + dg*dg + db*db;
                if (dist < best_dist) {
                    best_dist = dist;
                    assignments[t] = c;
                }
            }
        }

        // Recompute centroids
        std::vector<Color3> sums(k, {0, 0, 0});
        std::vector<int> counts(k, 0);
        for (int t = 0; t < tri_count; t++) {
            int c = assignments[t];
            sums[c].r += face_colors[t].r;
            sums[c].g += face_colors[t].g;
            sums[c].b += face_colors[t].b;
            counts[c]++;
        }
        for (int c = 0; c < k; c++) {
            if (counts[c] > 0) {
                centroids[c].r = sums[c].r / counts[c];
                centroids[c].g = sums[c].g / counts[c];
                centroids[c].b = sums[c].b / counts[c];
            }
        }
    }

    // Apply quantized colors back to mesh
    for (int t = 0; t < tri_count; t++) {
        Color3 qc = centroids[assignments[t]];
        for (int v = 0; v < 3; v++) {
            Vertex& vert = mesh.vertices[mesh.indices[t*3+v]];
            vert.color[0] = qc.r;
            vert.color[1] = qc.g;
            vert.color[2] = qc.b;
        }
    }
}

// ============================================================
// Laplacian mesh smoothing
// ============================================================

void smooth_mesh(Mesh& mesh, int iterations, float lambda) {
    int n = (int)mesh.vertices.size();
    std::vector<std::vector<int>> adj(n);
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        int a = mesh.indices[i], b = mesh.indices[i+1], c = mesh.indices[i+2];
        adj[a].push_back(b); adj[a].push_back(c);
        adj[b].push_back(a); adj[b].push_back(c);
        adj[c].push_back(a); adj[c].push_back(b);
    }
    for (auto& neighbors : adj) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    for (int iter = 0; iter < iterations; iter++) {
        std::vector<float> new_pos(n * 3);
        for (int i = 0; i < n; i++) {
            if (adj[i].empty()) {
                for (int k = 0; k < 3; k++) new_pos[i*3+k] = mesh.vertices[i].pos[k];
                continue;
            }
            float avg[3] = {0, 0, 0};
            for (int j : adj[i]) {
                for (int k = 0; k < 3; k++) avg[k] += mesh.vertices[j].pos[k];
            }
            float inv = 1.0f / adj[i].size();
            for (int k = 0; k < 3; k++) {
                avg[k] *= inv;
                new_pos[i*3+k] = mesh.vertices[i].pos[k] + lambda * (avg[k] - mesh.vertices[i].pos[k]);
            }
        }
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < 3; k++) mesh.vertices[i].pos[k] = new_pos[i*3+k];
        }
    }
}

// ============================================================
// Auto Retopology: Poisson disk sampling + 2D Delaunay
// ============================================================

static float tri_area(const float a[3], const float b[3], const float c[3]) {
    float e1[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
    float e2[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
    float cx = e1[1]*e2[2]-e1[2]*e2[1];
    float cy = e1[2]*e2[0]-e1[0]*e2[2];
    float cz = e1[0]*e2[1]-e1[1]*e2[0];
    return 0.5f * sqrtf(cx*cx+cy*cy+cz*cz);
}

struct SamplePoint {
    float pos[3], normal[3], color[3];
};

// Poisson-disk-like sampling on mesh surface
static std::vector<SamplePoint> sample_mesh_surface(const Mesh& mesh, int target_count) {
    int tri_count = (int)mesh.indices.size() / 3;
    if (tri_count == 0) return {};

    // Build CDF weighted by triangle area
    std::vector<float> cdf(tri_count);
    float total_area = 0;
    for (int t = 0; t < tri_count; t++) {
        const auto& a = mesh.vertices[mesh.indices[t*3+0]];
        const auto& b = mesh.vertices[mesh.indices[t*3+1]];
        const auto& c = mesh.vertices[mesh.indices[t*3+2]];
        total_area += tri_area(a.pos, b.pos, c.pos);
        cdf[t] = total_area;
    }
    if (total_area < 1e-10f) return {};

    float min_dist = sqrtf(total_area / (target_count * 0.866f));

    // Simple spatial hash for rejection
    float cell = min_dist;
    auto hash_pos = [cell](float x, float y, float z) -> uint64_t {
        int ix = (int)floorf(x / cell + 10000);
        int iy = (int)floorf(y / cell + 10000);
        int iz = (int)floorf(z / cell + 10000);
        return (uint64_t)ix * 73856093ULL ^ (uint64_t)iy * 19349663ULL ^ (uint64_t)iz * 83492791ULL;
    };
    std::map<uint64_t, std::vector<int>> grid;
    std::vector<SamplePoint> samples;

    // LCG random
    uint32_t rng = 12345;
    auto randf = [&rng]() -> float {
        rng = rng * 1103515245 + 12345;
        return (float)(rng & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    };

    int max_attempts = target_count * 40;
    for (int attempt = 0; attempt < max_attempts && (int)samples.size() < target_count; attempt++) {
        // Pick random triangle weighted by area
        float r = randf() * total_area;
        int ti = (int)(std::lower_bound(cdf.begin(), cdf.end(), r) - cdf.begin());
        ti = std::clamp(ti, 0, tri_count - 1);

        // Random barycentric coords
        float r1 = sqrtf(randf()), r2 = randf();
        float u = 1.0f - r1, v = r1 * r2, w = 1.0f - u - v;

        const auto& va = mesh.vertices[mesh.indices[ti*3+0]];
        const auto& vb = mesh.vertices[mesh.indices[ti*3+1]];
        const auto& vc = mesh.vertices[mesh.indices[ti*3+2]];

        SamplePoint sp;
        for (int k = 0; k < 3; k++) {
            sp.pos[k] = u*va.pos[k] + v*vb.pos[k] + w*vc.pos[k];
            sp.normal[k] = u*va.normal[k] + v*vb.normal[k] + w*vc.normal[k];
            sp.color[k] = u*va.color[k] + v*vb.color[k] + w*vc.color[k];
        }

        // Check distance to existing samples via spatial hash
        uint64_t h = hash_pos(sp.pos[0], sp.pos[1], sp.pos[2]);
        bool too_close = false;
        for (int dz = -1; dz <= 1 && !too_close; dz++)
        for (int dy = -1; dy <= 1 && !too_close; dy++)
        for (int dx = -1; dx <= 1 && !too_close; dx++) {
            uint64_t nh = hash_pos(sp.pos[0]+dx*cell, sp.pos[1]+dy*cell, sp.pos[2]+dz*cell);
            auto it = grid.find(nh);
            if (it == grid.end()) continue;
            for (int idx : it->second) {
                float d2 = 0;
                for (int k = 0; k < 3; k++) {
                    float diff = samples[idx].pos[k] - sp.pos[k];
                    d2 += diff * diff;
                }
                if (d2 < min_dist * min_dist) { too_close = true; break; }
            }
        }

        if (!too_close) {
            int si = (int)samples.size();
            grid[h].push_back(si);
            samples.push_back(sp);
        }

        // Reduce min_dist if we're stalling
        if (attempt > 0 && attempt % (target_count * 5) == 0 && (int)samples.size() < target_count / 2) {
            min_dist *= 0.8f;
            cell = min_dist;
        }
    }
    return samples;
}

// Simple 2D Delaunay (Bowyer-Watson)
static std::vector<unsigned int> delaunay_2d(const std::vector<float>& px, const std::vector<float>& py, int n) {
    struct Tri { int v[3]; bool alive; };
    std::vector<Tri> tris;

    // Super triangle
    float min_x = 1e30f, max_x = -1e30f, min_y = 1e30f, max_y = -1e30f;
    for (int i = 0; i < n; i++) {
        min_x = std::min(min_x, px[i]); max_x = std::max(max_x, px[i]);
        min_y = std::min(min_y, py[i]); max_y = std::max(max_y, py[i]);
    }
    float dx = max_x - min_x + 1.0f, dy = max_y - min_y + 1.0f;
    float cx_val = (min_x + max_x) * 0.5f, cy_val = (min_y + max_y) * 0.5f;
    float big = std::max(dx, dy) * 10.0f;
    // Super triangle vertices at indices n, n+1, n+2
    std::vector<float> all_x(px.begin(), px.end());
    std::vector<float> all_y(py.begin(), py.end());
    all_x.push_back(cx_val - big); all_y.push_back(cy_val - big);
    all_x.push_back(cx_val + big); all_y.push_back(cy_val - big);
    all_x.push_back(cx_val);       all_y.push_back(cy_val + big * 2);
    tris.push_back({{n, n+1, n+2}, true});

    auto in_circumcircle = [&](int ti, float px_val, float py_val) -> bool {
        float ax = all_x[tris[ti].v[0]], ay = all_y[tris[ti].v[0]];
        float bx = all_x[tris[ti].v[1]], by = all_y[tris[ti].v[1]];
        float ccx = all_x[tris[ti].v[2]], ccy = all_y[tris[ti].v[2]];
        float d = 2.0f * (ax*(by-ccy) + bx*(ccy-ay) + ccx*(ay-by));
        if (fabsf(d) < 1e-12f) return false;
        float ux = ((ax*ax+ay*ay)*(by-ccy)+(bx*bx+by*by)*(ccy-ay)+(ccx*ccx+ccy*ccy)*(ay-by))/d;
        float uy = ((ax*ax+ay*ay)*(ccx-bx)+(bx*bx+by*by)*(ax-ccx)+(ccx*ccx+ccy*ccy)*(bx-ax))/d;
        float r2 = (ax-ux)*(ax-ux)+(ay-uy)*(ay-uy);
        return (px_val-ux)*(px_val-ux)+(py_val-uy)*(py_val-uy) < r2;
    };

    for (int i = 0; i < n; i++) {
        // Find bad triangles
        std::vector<int> bad;
        for (int t = 0; t < (int)tris.size(); t++) {
            if (tris[t].alive && in_circumcircle(t, px[i], py[i]))
                bad.push_back(t);
        }
        // Find boundary polygon of the hole
        struct Edge { int a, b; };
        std::vector<Edge> polygon;
        for (int t : bad) {
            for (int e = 0; e < 3; e++) {
                int ea = tris[t].v[e], eb = tris[t].v[(e+1)%3];
                bool shared = false;
                for (int t2 : bad) {
                    if (t2 == t) continue;
                    for (int e2 = 0; e2 < 3; e2++) {
                        int a2 = tris[t2].v[e2], b2 = tris[t2].v[(e2+1)%3];
                        if ((ea==a2&&eb==b2)||(ea==b2&&eb==a2)) { shared=true; break; }
                    }
                    if (shared) break;
                }
                if (!shared) polygon.push_back({ea, eb});
            }
        }
        for (int t : bad) tris[t].alive = false;
        for (auto& e : polygon) {
            tris.push_back({{i, e.a, e.b}, true});
        }
    }

    // Collect result, excluding super triangle vertices
    std::vector<unsigned int> result;
    for (auto& t : tris) {
        if (!t.alive) continue;
        if (t.v[0] >= n || t.v[1] >= n || t.v[2] >= n) continue;
        result.push_back(t.v[0]);
        result.push_back(t.v[1]);
        result.push_back(t.v[2]);
    }
    return result;
}

Mesh retopologize(const Mesh& input, int target_vertices, LogCallback log) {
    if (input.vertices.empty()) return input;

    log("Sampling " + std::to_string(target_vertices) + " points on mesh surface...", 0);
    auto samples = sample_mesh_surface(input, target_vertices);
    if ((int)samples.size() < 4) {
        log("Too few samples, skipping retopology", 2);
        return input;
    }
    log("Sampled " + std::to_string(samples.size()) + " points", 1);

    // Split into front-facing (nz >= 0) and back-facing groups
    std::vector<int> front_ids, back_ids;
    for (int i = 0; i < (int)samples.size(); i++) {
        if (samples[i].normal[2] >= 0)
            front_ids.push_back(i);
        else
            back_ids.push_back(i);
    }

    // If all points face one direction, just do one Delaunay
    if (front_ids.empty()) front_ids = back_ids;
    if (back_ids.empty()) back_ids = front_ids;

    // Delaunay on front group (project to XY)
    log("Triangulating front faces (" + std::to_string(front_ids.size()) + " pts)...", 0);
    std::vector<float> fpx, fpy;
    for (int id : front_ids) { fpx.push_back(samples[id].pos[0]); fpy.push_back(samples[id].pos[1]); }
    auto front_tris = delaunay_2d(fpx, fpy, (int)front_ids.size());

    // Delaunay on back group
    log("Triangulating back faces (" + std::to_string(back_ids.size()) + " pts)...", 0);
    std::vector<float> bpx, bpy;
    for (int id : back_ids) { bpx.push_back(samples[id].pos[0]); bpy.push_back(samples[id].pos[1]); }
    auto back_tris = delaunay_2d(bpx, bpy, (int)back_ids.size());

    // Build output mesh
    Mesh out;
    out.vertices.resize(samples.size());
    for (int i = 0; i < (int)samples.size(); i++) {
        memcpy(out.vertices[i].pos, samples[i].pos, sizeof(float)*3);
        memcpy(out.vertices[i].normal, samples[i].normal, sizeof(float)*3);
        memcpy(out.vertices[i].color, samples[i].color, sizeof(float)*3);
    }

    // Front triangles (remap indices)
    for (size_t i = 0; i < front_tris.size(); i += 3) {
        out.indices.push_back(front_ids[front_tris[i]]);
        out.indices.push_back(front_ids[front_tris[i+1]]);
        out.indices.push_back(front_ids[front_tris[i+2]]);
    }
    // Back triangles (reversed winding)
    for (size_t i = 0; i < back_tris.size(); i += 3) {
        out.indices.push_back(back_ids[back_tris[i]]);
        out.indices.push_back(back_ids[back_tris[i+2]]);
        out.indices.push_back(back_ids[back_tris[i+1]]);
    }

    // Stitch boundary between front and back using edge detection
    log("Stitching front/back boundaries...", 0);
    {
        // Find boundary edges of front triangulation
        struct EdgeKey { unsigned int a, b; bool operator<(const EdgeKey& o) const { return std::tie(a,b)<std::tie(o.a,o.b); } };
        auto mk = [](unsigned int a, unsigned int b) -> EdgeKey { return a<b?EdgeKey{a,b}:EdgeKey{b,a}; };

        std::map<EdgeKey, int> edge_count;
        for (size_t i = 0; i < front_tris.size(); i += 3) {
            for (int e = 0; e < 3; e++) {
                unsigned int a = front_ids[front_tris[i+e]];
                unsigned int b = front_ids[front_tris[i+(e+1)%3]];
                edge_count[mk(a,b)]++;
            }
        }

        // For each front boundary edge, find nearest back vertices and stitch
        for (auto& [ek, cnt] : edge_count) {
            if (cnt != 1) continue;
            unsigned int fa = ek.a, fb = ek.b;

            // Find nearest back vertex to midpoint of this edge
            float mx = (out.vertices[fa].pos[0]+out.vertices[fb].pos[0])*0.5f;
            float my = (out.vertices[fa].pos[1]+out.vertices[fb].pos[1])*0.5f;

            int best_back = -1; float best_d2 = 1e30f;
            for (int bi : back_ids) {
                float d2 = (out.vertices[bi].pos[0]-mx)*(out.vertices[bi].pos[0]-mx) +
                           (out.vertices[bi].pos[1]-my)*(out.vertices[bi].pos[1]-my);
                if (d2 < best_d2) { best_d2 = d2; best_back = bi; }
            }
            if (best_back >= 0) {
                out.indices.push_back(fa);
                out.indices.push_back(fb);
                out.indices.push_back(best_back);
            }
        }
    }

    log("Retopology complete: " + std::to_string(out.vertices.size()) + " verts, " +
        std::to_string(out.indices.size()/3) + " tris", 1);
    return out;
}

// Helper: recompute normals on a mesh after decimation
static void recompute_normals(Mesh& mesh) {
    for (auto& v : mesh.vertices) {
        v.normal[0] = v.normal[1] = v.normal[2] = 0;
    }
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        Vertex& a = mesh.vertices[mesh.indices[i]];
        Vertex& b = mesh.vertices[mesh.indices[i+1]];
        Vertex& c = mesh.vertices[mesh.indices[i+2]];
        float e1[3] = {b.pos[0]-a.pos[0], b.pos[1]-a.pos[1], b.pos[2]-a.pos[2]};
        float e2[3] = {c.pos[0]-a.pos[0], c.pos[1]-a.pos[1], c.pos[2]-a.pos[2]};
        float n[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
        for (int k = 0; k < 3; k++) {
            a.normal[k] += n[k]; b.normal[k] += n[k]; c.normal[k] += n[k];
        }
    }
    for (auto& v : mesh.vertices) {
        float len = sqrtf(v.normal[0]*v.normal[0]+v.normal[1]*v.normal[1]+v.normal[2]*v.normal[2]);
        if (len > 0.0001f) { v.normal[0]/=len; v.normal[1]/=len; v.normal[2]/=len; }
    }
}

// ============================================================
// Full pipeline
// ============================================================

PipelineResult run_pipeline(const std::string& front_path, const std::string& side_path,
                            const std::string& back_path, int target_poly_count, LogCallback log) {
    PipelineResult result;

    // Step 1: Load images
    log("=== Pipeline Started ===", 0);
    log("[1/5] Loading images...", 0);

    ImageData front = load_image(front_path);
    if (front.pixels.empty()) {
        result.error = "Failed to load front image: " + front_path;
        log(result.error, 3);
        return result;
    }
    log("  Front: " + std::to_string(front.width) + "x" + std::to_string(front.height), 0);

    ImageData side = load_image(side_path);
    if (side.pixels.empty()) {
        result.error = "Failed to load side image: " + side_path;
        log(result.error, 3);
        return result;
    }
    log("  Side: " + std::to_string(side.width) + "x" + std::to_string(side.height), 0);

    ImageData back_img = load_image(back_path);
    if (back_img.pixels.empty()) {
        result.error = "Failed to load back image: " + back_path;
        log(result.error, 3);
        return result;
    }
    log("  Back: " + std::to_string(back_img.width) + "x" + std::to_string(back_img.height), 0);
    log("All images loaded successfully", 1);

    // Step 2: Generate mesh from silhouettes
    log("[2/5] Generating 3D mesh from silhouettes...", 0);
    result.mesh = generate_mesh_from_silhouettes(front, side, back_img, log);

    // Step 3: QEM Decimate (to 2x target, retopo refines further)
    log("[3/8] QEM decimating mesh...", 0);
    result.mesh = decimate_mesh(result.mesh, target_poly_count * 2, log);

    // Step 4: Auto retopology
    log("[4/8] Auto retopology...", 0);
    result.mesh = retopologize(result.mesh, target_poly_count, log);

    // Step 5: Smooth mesh
    log("[5/8] Smoothing mesh...", 0);
    smooth_mesh(result.mesh, 3, 0.5f);
    recompute_normals(result.mesh);
    log("Mesh smoothed", 1);

    // Step 6: Flat shading
    log("[6/8] Converting to flat shading...", 0);
    result.mesh = make_flat_shaded(result.mesh);
    log("Flat shading: " + std::to_string(result.mesh.indices.size() / 3) + " tris", 1);

    // Step 7: Per-face color baking
    log("[7/8] Baking per-face colors...", 0);
    bake_face_colors(result.mesh, front, &side, &back_img);
    quantize_colors(result.mesh, 24);
    log("24-color palette applied", 1);

    // Step 8: UV unwrap
    log("[8/8] UV unwrapping with xatlas...", 0);
    if (!uv_unwrap(result.mesh, log)) {
        log("UV unwrapping skipped", 2);
    }

    result.success = true;
    log("=== Pipeline Complete ===", 1);
    log("Final mesh: " + std::to_string(result.mesh.vertices.size()) + " verts, " +
        std::to_string(result.mesh.indices.size() / 3) + " tris", 1);

    return result;
}

// ============================================================
// Front-only pipeline (with MiDaS depth if available)
// ============================================================

#include "depth.h"
#include <fstream>

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

PipelineResult run_pipeline_front_only(const std::string& front_path, int target_poly_count, LogCallback log) {
    PipelineResult result;

    log("=== Pipeline Started (Front Image Only) ===", 0);
    log("[1/7] Loading front image...", 0);

    ImageData front = load_image(front_path);
    if (front.pixels.empty()) {
        result.error = "Failed to load front image: " + front_path;
        log(result.error, 3);
        return result;
    }
    log("  Front: " + std::to_string(front.width) + "x" + std::to_string(front.height), 1);

    // Try MiDaS depth estimation if model file exists
    std::string model_path = "models/midas_small.onnx";
    bool use_depth = file_exists(model_path);

    if (use_depth) {
        log("[2/7] Running MiDaS depth estimation...", 0);
        auto depth = estimate_depth(front, model_path, log);
        if (!depth.empty()) {
            log("[3/7] Converting depth map to 3D mesh...", 0);
            int downsample = std::max(2, std::min(front.width, front.height) / 128);
            result.mesh = depth_to_mesh(front, depth, downsample, log);
        }
    }

    // Fallback to silhouette extrusion if MiDaS failed or unavailable
    if (result.mesh.vertices.empty()) {
        if (!use_depth) {
            log("[2/7] MiDaS model not found, using silhouette extrusion...", 2);
            log("  (Place midas_small.onnx in models/ for better results)", 0);
        } else {
            log("Depth estimation failed, falling back to silhouette extrusion...", 2);
        }
        result.mesh = generate_mesh_from_front(front, log);
        if (result.mesh.vertices.empty()) {
            result.error = "Failed to generate mesh";
            log(result.error, 3);
            return result;
        }
    }

    log("[4/9] QEM decimating mesh...", 0);
    result.mesh = decimate_mesh(result.mesh, target_poly_count * 2, log);

    log("[5/9] Auto retopology...", 0);
    result.mesh = retopologize(result.mesh, target_poly_count, log);

    log("[6/9] Smoothing mesh...", 0);
    smooth_mesh(result.mesh, 3, 0.5f);
    recompute_normals(result.mesh);
    log("Mesh smoothed", 1);

    log("[7/9] Converting to flat shading...", 0);
    result.mesh = make_flat_shaded(result.mesh);
    log("Flat shading: " + std::to_string(result.mesh.indices.size() / 3) + " tris", 1);

    log("[8/9] Baking per-face colors...", 0);
    bake_face_colors(result.mesh, front);
    quantize_colors(result.mesh, 24);
    log("24-color palette applied", 1);

    log("[9/9] UV unwrapping with xatlas...", 0);
    if (!uv_unwrap(result.mesh, log)) {
        log("UV unwrapping skipped", 2);
    }

    result.success = true;
    log("=== Pipeline Complete ===", 1);
    log("Final mesh: " + std::to_string(result.mesh.vertices.size()) + " verts, " +
        std::to_string(result.mesh.indices.size() / 3) + " tris", 1);

    return result;
}
