#include "app.h"
#include <nfd.h>
#include <algorithm>
#include <ctime>

std::string open_image_dialog() {
    NFD_Init();
    nfdchar_t* out_path = nullptr;
    nfdfilteritem_t filters[1] = {{"Images", "png,jpg,jpeg,bmp,tga"}};
    nfdresult_t result = NFD_OpenDialog(&out_path, filters, 1, nullptr);
    std::string path;
    if (result == NFD_OKAY) {
        path = out_path;
        NFD_FreePath(out_path);
    }
    NFD_Quit();
    return path;
}

const char* setup_glfw_hints() {
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    return "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    return "#version 130";
#endif
}

bool validate_poly_count(int count) {
    return count >= 500 && count <= 20000;
}

int clamp_poly_count(int count) {
    return std::clamp(count, 500, 20000);
}

bool can_generate(const AppState& state) {
    if (state.mode == GenerationMode::FrontOnly) {
        return !state.front_image_path.empty();
    }
    return !state.front_image_path.empty()
        && !state.side_image_path.empty()
        && !state.back_image_path.empty();
}

void AppState::reset() {
    front_image_path.clear();
    side_image_path.clear();
    back_image_path.clear();
    generate_requested = false;
    stage = PipelineStage::Idle;
    progress = 0.0f;
    logs.clear();
    add_info("Reset complete. Ready for new generation.");
}

static std::string get_timestamp() {
    auto now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
    return buf;
}

void AppState::add_log(const std::string& msg, ImVec4 color) {
    logs.push_back({get_timestamp(), msg, color});
}

void AppState::add_info(const std::string& msg) {
    add_log(msg, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
}

void AppState::add_success(const std::string& msg) {
    add_log(msg, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
}

void AppState::add_warning(const std::string& msg) {
    add_log(msg, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
}

void AppState::add_error(const std::string& msg) {
    add_log(msg, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
}

const char* stage_name(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::Idle:           return "Idle";
        case PipelineStage::Preprocessing:  return "Preprocessing Images";
        case PipelineStage::Inference:      return "Running AI Inference";
        case PipelineStage::Decimation:     return "Decimating Mesh";
        case PipelineStage::UVUnwrap:       return "UV Unwrapping";
        case PipelineStage::TextureProject: return "Projecting Textures";
        case PipelineStage::Done:           return "Complete";
        case PipelineStage::Error:          return "Error";
    }
    return "Unknown";
}
