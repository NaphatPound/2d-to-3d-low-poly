#include "viewport.h"
#include <cstring>
#include <cstdio>

#if !defined(__APPLE__)
#include <GL/gl.h>
#endif

// ----- Simple math helpers -----
static void normalize(float v[3]) {
    float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 0.0001f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

static void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static float dot(const float a[3], const float b[3]) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

// Column-major 4x4 matrix helpers
static void mat4_identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_perspective(float m[16], float fovy_deg, float aspect, float near, float far) {
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fovy_deg * 0.5f * 3.14159265f / 180.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void mat4_lookat(float m[16], const float eye[3], const float center[3], const float up[3]) {
    float f[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
    normalize(f);
    float s[3]; cross(f, up, s); normalize(s);
    float u[3]; cross(s, f, u);
    mat4_identity(m);
    m[0] = s[0]; m[4] = s[1]; m[8]  = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9]  = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[12] = -dot(s, eye);
    m[13] = -dot(u, eye);
    m[14] = dot(f, eye);
}

// ----- Shaders -----
static const char* vert_src =
    "#version 150\n"
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "in vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "flat out vec3 vNormal;\n"
    "flat out vec3 vColor;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "    vColor = aColor;\n"
    "    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;\n"
    "}\n";

static const char* frag_src =
    "#version 150\n"
    "flat in vec3 vNormal;\n"
    "flat in vec3 vColor;\n"
    "in vec3 vWorldPos;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.8));\n"
    "    vec3 n = normalize(vNormal);\n"
    "    float diff = max(dot(n, lightDir), 0.0);\n"
    "    float ambient = 0.3;\n"
    "    vec3 color = vColor * (ambient + diff * 0.7);\n"
    "    FragColor = vec4(color, 1.0);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

// ----- Demo mesh: low-poly humanoid -----

static void add_box(Mesh& mesh, float cx, float cy, float cz,
                     float sx, float sy, float sz,
                     float r, float g, float b) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    // 8 corners
    float corners[8][3] = {
        {cx-hx, cy-hy, cz-hz}, {cx+hx, cy-hy, cz-hz},
        {cx+hx, cy+hy, cz-hz}, {cx-hx, cy+hy, cz-hz},
        {cx-hx, cy-hy, cz+hz}, {cx+hx, cy-hy, cz+hz},
        {cx+hx, cy+hy, cz+hz}, {cx-hx, cy+hy, cz+hz}
    };
    // 6 faces (2 tris each), with normals
    int faces[6][4] = {
        {0,1,2,3}, {5,4,7,6}, // front, back
        {4,0,3,7}, {1,5,6,2}, // left, right
        {3,2,6,7}, {4,5,1,0}  // top, bottom
    };
    float normals[6][3] = {
        {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}
    };

    unsigned int base = (unsigned int)mesh.vertices.size();
    for (int f = 0; f < 6; f++) {
        for (int v = 0; v < 4; v++) {
            Vertex vtx;
            memcpy(vtx.pos, corners[faces[f][v]], sizeof(float)*3);
            memcpy(vtx.normal, normals[f], sizeof(float)*3);
            vtx.color[0] = r; vtx.color[1] = g; vtx.color[2] = b;
            mesh.vertices.push_back(vtx);
        }
        unsigned int i = base + f * 4;
        mesh.indices.push_back(i); mesh.indices.push_back(i+1); mesh.indices.push_back(i+2);
        mesh.indices.push_back(i); mesh.indices.push_back(i+2); mesh.indices.push_back(i+3);
    }
}

Mesh generate_demo_mesh() {
    Mesh mesh;
    // Head
    add_box(mesh, 0.0f, 1.75f, 0.0f, 0.5f, 0.5f, 0.5f, 0.9f, 0.75f, 0.65f);
    // Torso
    add_box(mesh, 0.0f, 1.0f, 0.0f, 0.7f, 0.9f, 0.4f, 0.3f, 0.5f, 0.8f);
    // Left arm
    add_box(mesh, -0.55f, 1.0f, 0.0f, 0.25f, 0.8f, 0.25f, 0.3f, 0.5f, 0.8f);
    // Right arm
    add_box(mesh, 0.55f, 1.0f, 0.0f, 0.25f, 0.8f, 0.25f, 0.3f, 0.5f, 0.8f);
    // Left leg
    add_box(mesh, -0.2f, 0.15f, 0.0f, 0.3f, 0.9f, 0.3f, 0.25f, 0.25f, 0.35f);
    // Right leg
    add_box(mesh, 0.2f, 0.15f, 0.0f, 0.3f, 0.9f, 0.3f, 0.25f, 0.25f, 0.35f);
    return mesh;
}

// ----- Renderer -----

bool Renderer::init() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vs);
    glAttachShader(shader_program, fs);
    glBindAttribLocation(shader_program, 0, "aPos");
    glBindAttribLocation(shader_program, 1, "aNormal");
    glBindAttribLocation(shader_program, 2, "aColor");
    glLinkProgram(shader_program);
    GLint ok; glGetProgramiv(shader_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(shader_program, 512, nullptr, log);
        fprintf(stderr, "Shader link error: %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    return true;
}

void Renderer::upload_mesh(const Mesh& mesh) {
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);

    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    // color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glBindVertexArray(0);
    index_count = (int)mesh.indices.size();
}

void Renderer::resize_framebuffer(int width, int height) {
    if (width == fb_width && height == fb_height && fbo != 0) return;
    if (width <= 0 || height <= 0) return;
    fb_width = width;
    fb_height = height;

    if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &color_tex); glDeleteRenderbuffers(1, &depth_rbo); }

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);

    glGenRenderbuffers(1, &depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete: 0x%x\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void mat4_multiply(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            out[c*4+r] = 0;
            for (int k = 0; k < 4; k++)
                out[c*4+r] += a[k*4+r] * b[c*4+k];
        }
}

void Renderer::render(const Camera& cam, int width, int height) {
    if (index_count == 0 || width <= 0 || height <= 0) return;

    resize_framebuffer(width, height);

    // Save GL state that ImGui may have set
    GLint prev_fbo; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vao; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    GLint prev_program; glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    GLint prev_viewport[4]; glGetIntegerv(GL_VIEWPORT, prev_viewport);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Camera position from spherical coords
    float rad_yaw = cam.yaw * 3.14159265f / 180.0f;
    float rad_pitch = cam.pitch * 3.14159265f / 180.0f;
    float eye[3] = {
        cam.target[0] + cam.distance * cosf(rad_pitch) * cosf(rad_yaw),
        cam.target[1] + cam.distance * sinf(rad_pitch),
        cam.target[2] + cam.distance * cosf(rad_pitch) * sinf(rad_yaw)
    };
    float up[3] = {0, 1, 0};

    float view[16], proj[16], model[16], vp[16], mvp[16];
    mat4_lookat(view, eye, cam.target, up);
    mat4_perspective(proj, 45.0f, (float)width / (float)height, 0.1f, 100.0f);
    mat4_identity(model);
    mat4_multiply(proj, view, vp);
    mat4_multiply(vp, model, mvp);

    glUseProgram(shader_program);
    glUniformMatrix4fv(glGetUniformLocation(shader_program, "uMVP"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(glGetUniformLocation(shader_program, "uModel"), 1, GL_FALSE, model);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glBindVertexArray(prev_vao);
    glUseProgram(prev_program);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

void Renderer::render_direct(const Camera& cam, int width, int height) {
    if (index_count == 0 || width <= 0 || height <= 0) return;

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, width, height);

    float rad_yaw = cam.yaw * 3.14159265f / 180.0f;
    float rad_pitch = cam.pitch * 3.14159265f / 180.0f;
    float eye[3] = {
        cam.target[0] + cam.distance * cosf(rad_pitch) * cosf(rad_yaw),
        cam.target[1] + cam.distance * sinf(rad_pitch),
        cam.target[2] + cam.distance * cosf(rad_pitch) * sinf(rad_yaw)
    };
    float up[3] = {0, 1, 0};

    float view[16], proj[16], model_m[16], vp[16], mvp[16];
    mat4_lookat(view, eye, cam.target, up);
    mat4_perspective(proj, 45.0f, (float)width / (float)height, 0.1f, 100.0f);
    mat4_identity(model_m);
    mat4_multiply(proj, view, vp);
    mat4_multiply(vp, model_m, mvp);

    glUseProgram(shader_program);
    glUniformMatrix4fv(glGetUniformLocation(shader_program, "uMVP"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(glGetUniformLocation(shader_program, "uModel"), 1, GL_FALSE, model_m);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
}

void Renderer::cleanup() {
    if (shader_program) glDeleteProgram(shader_program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &color_tex); glDeleteRenderbuffers(1, &depth_rbo); }
}
