#pragma once

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <string>
#include <vector>
#include <chrono>

struct LogEntry {
    std::string timestamp;
    std::string message;
    ImVec4 color;
};

struct AppConfig {
    int window_width = 1280;
    int window_height = 720;
    const char* window_title = "2D to 3D Low-Poly Character Generator";
    float clear_color[4] = {0.15f, 0.15f, 0.15f, 1.00f};
};

enum class GenerationMode {
    FrontOnly = 0,
    ThreeView = 1
};

enum class PipelineStage {
    Idle,
    Preprocessing,
    Inference,
    Decimation,
    UVUnwrap,
    TextureProject,
    Done,
    Error
};

struct AppState {
    GenerationMode mode = GenerationMode::FrontOnly;
    int poly_count_limit = 5000;
    std::string front_image_path;
    std::string side_image_path;
    std::string back_image_path;
    bool generate_requested = false;
    PipelineStage stage = PipelineStage::Idle;
    float progress = 0.0f;
    std::vector<LogEntry> logs;

    void add_log(const std::string& msg, ImVec4 color = ImVec4(1, 1, 1, 1));
    void add_info(const std::string& msg);
    void add_success(const std::string& msg);
    void add_warning(const std::string& msg);
    void add_error(const std::string& msg);
    void reset();
};

const char* stage_name(PipelineStage stage);

// Opens a native file dialog for image selection. Returns path or empty string on cancel.
std::string open_image_dialog();

// Returns the appropriate GLSL version string and sets GLFW window hints
const char* setup_glfw_hints();

// Validates polygon count is within allowed range [500, 20000]
bool validate_poly_count(int count);

// Clamps polygon count to allowed range
int clamp_poly_count(int count);

// Checks if required images are set based on generation mode
bool can_generate(const AppState& state);
