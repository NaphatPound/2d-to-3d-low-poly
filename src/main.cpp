#include "app.h"
#include "viewport.h"
#include "pipeline.h"
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << "\n";
}

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = setup_glfw_hints();

    AppConfig config;
    AppState state;
    Camera camera;
    Renderer renderer;
    bool has_model = false;

    GLFWwindow* window = glfwCreateWindow(config.window_width, config.window_height, config.window_title, nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    renderer.init();

    // Load a demo mesh immediately so we can verify rendering works
    {
        Mesh demo = generate_demo_mesh();
        renderer.upload_mesh(demo);
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        // ===== 1. Render 3D scene to default framebuffer =====
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (renderer.index_count > 0) {
            renderer.render_direct(camera, display_w, display_h);
        }

        // ===== 2. Build ImGui UI on top =====
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- Settings Panel (left side) ---
        float panel_width = 380.0f;
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(panel_width, (float)display_h * 0.6f), ImGuiCond_FirstUseEver);
        ImGui::Begin("2D to 3D Generation Settings");

        // --- Generation Mode ---
        ImGui::Text("Generation Mode:");
        int mode_idx = (int)state.mode;
        ImGui::RadioButton("Front Image Only", &mode_idx, 0);
        ImGui::SameLine();
        ImGui::RadioButton("3-View (Front + Side + Back)", &mode_idx, 1);
        state.mode = (GenerationMode)mode_idx;

        ImGui::Separator();

        // --- Image Selection ---
        ImGui::Text("Select Images:");
        if (ImGui::Button("Select Front Image")) {
            auto path = open_image_dialog();
            if (!path.empty()) {
                state.front_image_path = path;
                state.add_success("Front image loaded: " + path);
            }
        }
        if (!state.front_image_path.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "OK");
        }

        if (state.mode == GenerationMode::ThreeView) {
            if (ImGui::Button("Select Side Image")) {
                auto path = open_image_dialog();
                if (!path.empty()) {
                    state.side_image_path = path;
                    state.add_success("Side image loaded: " + path);
                }
            }
            if (!state.side_image_path.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "OK");
            }

            if (ImGui::Button("Select Back Image")) {
                auto path = open_image_dialog();
                if (!path.empty()) {
                    state.back_image_path = path;
                    state.add_success("Back image loaded: " + path);
                }
            }
            if (!state.back_image_path.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "OK");
            }
        }

        // Show loaded paths
        if (!state.front_image_path.empty())
            ImGui::TextWrapped("Front: %s", state.front_image_path.c_str());
        if (state.mode == GenerationMode::ThreeView) {
            if (!state.side_image_path.empty())
                ImGui::TextWrapped("Side:  %s", state.side_image_path.c_str());
            if (!state.back_image_path.empty())
                ImGui::TextWrapped("Back:  %s", state.back_image_path.c_str());
        }

        ImGui::Separator();
        ImGui::SliderInt("Target Polygon Count", &state.poly_count_limit, 500, 20000);

        ImGui::Spacing();

        // --- Generate Button ---
        bool ready = can_generate(state);
        if (!ready) ImGui::BeginDisabled();
        if (ImGui::Button("Generate 3D Model", ImVec2(200, 40))) {
            state.generate_requested = true;
            state.stage = PipelineStage::Preprocessing;
            state.progress = 0.0f;

            auto log_cb = [&state](const std::string& msg, int level) {
                switch (level) {
                    case 1: state.add_success(msg); break;
                    case 2: state.add_warning(msg); break;
                    case 3: state.add_error(msg); break;
                    default: state.add_info(msg); break;
                }
            };

            state.add_info("Target polygon count: " + std::to_string(state.poly_count_limit));

            PipelineResult result;
            if (state.mode == GenerationMode::FrontOnly) {
                result = run_pipeline_front_only(
                    state.front_image_path, state.poly_count_limit, log_cb);
            } else {
                result = run_pipeline(
                    state.front_image_path, state.side_image_path, state.back_image_path,
                    state.poly_count_limit, log_cb);
            }

            if (result.success) {
                renderer.upload_mesh(result.mesh);
                has_model = true;
                camera.yaw = -90.0f;
                camera.pitch = 15.0f;
                camera.distance = 3.0f;
                camera.target[0] = 0.0f;
                camera.target[1] = 0.0f;
                camera.target[2] = 0.0f;
                state.progress = 1.0f;
                state.stage = PipelineStage::Done;
                state.add_success("Model ready! Rotate the 3D view with your mouse.");
            } else {
                state.stage = PipelineStage::Error;
                state.add_error("Pipeline failed: " + result.error);
            }
        }
        if (!ready) ImGui::EndDisabled();

        // --- Reset Button ---
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(80, 40))) {
            state.reset();
            has_model = false;
            // Reload demo mesh
            Mesh demo = generate_demo_mesh();
            renderer.upload_mesh(demo);
            camera.yaw = -90.0f;
            camera.pitch = 20.0f;
            camera.distance = 5.0f;
            camera.target[0] = 0.0f;
            camera.target[1] = 0.5f;
            camera.target[2] = 0.0f;
        }

        if (state.stage != PipelineStage::Idle) {
            ImGui::Separator();
            ImGui::Text("Status: %s", stage_name(state.stage));
            ImGui::ProgressBar(state.progress, ImVec2(-1, 0));
        }

        ImGui::End();

        // --- Log Window (bottom left) ---
        ImGui::SetNextWindowPos(ImVec2(0, (float)display_h * 0.6f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(panel_width, (float)display_h * 0.4f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Log");
        if (ImGui::Button("Clear Log")) {
            state.logs.clear();
        }
        ImGui::Separator();
        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& entry : state.logs) {
            ImGui::TextColored(entry.color, "[%s] %s", entry.timestamp.c_str(), entry.message.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();

        // --- 3D Viewport overlay (shows controls hint) ---
        {
            ImGui::SetNextWindowPos(ImVec2(panel_width + 10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(250, 60), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Left-drag: Rotate");
            ImGui::Text("Scroll: Zoom | Right-drag: Pan");
            ImGui::End();
        }

        // --- Mouse controls on the 3D viewport area (not over ImGui windows) ---
        if (!io.WantCaptureMouse) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                camera.yaw += delta.x * 0.3f;
                camera.pitch += delta.y * 0.3f;
                if (camera.pitch > 89.0f) camera.pitch = 89.0f;
                if (camera.pitch < -89.0f) camera.pitch = -89.0f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
            float scroll = io.MouseWheel;
            if (scroll != 0.0f) {
                camera.distance -= scroll * 0.5f;
                if (camera.distance < 1.0f) camera.distance = 1.0f;
                if (camera.distance > 20.0f) camera.distance = 20.0f;
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                camera.target[1] += delta.y * 0.005f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }

        // ===== 3. Render ImGui draw data on top of 3D scene =====
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    renderer.cleanup();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
