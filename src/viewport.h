#pragma once

#define GL_SILENCE_DEPRECATION
#if defined(__APPLE__)
#define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#include <OpenGL/gl3.h>
#endif
#include <GLFW/glfw3.h>
#include <vector>
#include <cmath>

struct Vertex {
    float pos[3];
    float normal[3];
    float color[3];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

struct Camera {
    float yaw = -90.0f;
    float pitch = 20.0f;
    float distance = 5.0f;
    float target[3] = {0.0f, 0.5f, 0.0f};
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;
    bool dragging = false;
};

// Generate a demo low-poly humanoid mesh
Mesh generate_demo_mesh();

// OpenGL rendering
struct Renderer {
    GLuint shader_program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int index_count = 0;
    GLuint fbo = 0;
    GLuint color_tex = 0;
    GLuint depth_rbo = 0;
    int fb_width = 0;
    int fb_height = 0;

    bool init();
    void upload_mesh(const Mesh& mesh);
    void resize_framebuffer(int width, int height);
    void render(const Camera& cam, int width, int height);
    void render_direct(const Camera& cam, int width, int height);
    void cleanup();
};
