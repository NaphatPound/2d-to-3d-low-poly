#include "../src/app.h"
#include <cassert>
#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  TEST: " << #name << " ... "; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "PASSED\n"; \
    } while(0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cout << "FAILED at line " << __LINE__ << ": " #expr "\n"; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cout << "FAILED at line " << __LINE__ << ": " #a " != " #b "\n"; \
            return; \
        } \
    } while(0)

// --- Tests ---

void test_validate_poly_count_valid() {
    TEST(validate_poly_count_valid);
    ASSERT_TRUE(validate_poly_count(500));
    ASSERT_TRUE(validate_poly_count(5000));
    ASSERT_TRUE(validate_poly_count(20000));
    ASSERT_TRUE(validate_poly_count(10000));
    PASS();
}

void test_validate_poly_count_invalid() {
    TEST(validate_poly_count_invalid);
    ASSERT_TRUE(!validate_poly_count(499));
    ASSERT_TRUE(!validate_poly_count(20001));
    ASSERT_TRUE(!validate_poly_count(0));
    ASSERT_TRUE(!validate_poly_count(-1));
    PASS();
}

void test_clamp_poly_count() {
    TEST(clamp_poly_count);
    ASSERT_EQ(clamp_poly_count(100), 500);
    ASSERT_EQ(clamp_poly_count(500), 500);
    ASSERT_EQ(clamp_poly_count(5000), 5000);
    ASSERT_EQ(clamp_poly_count(20000), 20000);
    ASSERT_EQ(clamp_poly_count(30000), 20000);
    ASSERT_EQ(clamp_poly_count(-5), 500);
    PASS();
}

void test_can_generate_three_view_all_set() {
    TEST(can_generate_three_view_all_set);
    AppState state;
    state.mode = GenerationMode::ThreeView;
    state.front_image_path = "/path/front.png";
    state.side_image_path = "/path/side.png";
    state.back_image_path = "/path/back.png";
    ASSERT_TRUE(can_generate(state));
    PASS();
}

void test_can_generate_three_view_missing_front() {
    TEST(can_generate_three_view_missing_front);
    AppState state;
    state.mode = GenerationMode::ThreeView;
    state.side_image_path = "/path/side.png";
    state.back_image_path = "/path/back.png";
    ASSERT_TRUE(!can_generate(state));
    PASS();
}

void test_can_generate_three_view_missing_side() {
    TEST(can_generate_three_view_missing_side);
    AppState state;
    state.mode = GenerationMode::ThreeView;
    state.front_image_path = "/path/front.png";
    state.back_image_path = "/path/back.png";
    ASSERT_TRUE(!can_generate(state));
    PASS();
}

void test_can_generate_three_view_missing_back() {
    TEST(can_generate_three_view_missing_back);
    AppState state;
    state.mode = GenerationMode::ThreeView;
    state.front_image_path = "/path/front.png";
    state.side_image_path = "/path/side.png";
    ASSERT_TRUE(!can_generate(state));
    PASS();
}

void test_can_generate_front_only_with_front() {
    TEST(can_generate_front_only_with_front);
    AppState state;
    state.mode = GenerationMode::FrontOnly;
    state.front_image_path = "/path/front.png";
    ASSERT_TRUE(can_generate(state));
    PASS();
}

void test_can_generate_front_only_empty() {
    TEST(can_generate_front_only_empty);
    AppState state;
    state.mode = GenerationMode::FrontOnly;
    ASSERT_TRUE(!can_generate(state));
    PASS();
}

void test_reset() {
    TEST(reset);
    AppState state;
    state.front_image_path = "/path/front.png";
    state.side_image_path = "/path/side.png";
    state.stage = PipelineStage::Done;
    state.progress = 1.0f;
    state.reset();
    ASSERT_TRUE(state.front_image_path.empty());
    ASSERT_TRUE(state.side_image_path.empty());
    ASSERT_TRUE(state.back_image_path.empty());
    ASSERT_EQ((int)state.stage, (int)PipelineStage::Idle);
    ASSERT_TRUE(state.progress < 0.01f);
    PASS();
}

void test_app_config_defaults() {
    TEST(app_config_defaults);
    AppConfig config;
    ASSERT_EQ(config.window_width, 1280);
    ASSERT_EQ(config.window_height, 720);
    ASSERT_TRUE(config.clear_color[0] > 0.14f && config.clear_color[0] < 0.16f);
    PASS();
}

void test_app_state_defaults() {
    TEST(app_state_defaults);
    AppState state;
    ASSERT_EQ(state.poly_count_limit, 5000);
    ASSERT_TRUE(state.front_image_path.empty());
    ASSERT_TRUE(state.side_image_path.empty());
    ASSERT_TRUE(state.back_image_path.empty());
    ASSERT_TRUE(!state.generate_requested);
    PASS();
}

void test_glfw_init() {
    TEST(glfw_init);
    ASSERT_TRUE(glfwInit());
    glfwTerminate();
    PASS();
}

int main() {
    std::cout << "=== 2D to 3D Low-Poly Tests ===\n\n";

    test_validate_poly_count_valid();
    test_validate_poly_count_invalid();
    test_clamp_poly_count();
    test_can_generate_three_view_all_set();
    test_can_generate_three_view_missing_front();
    test_can_generate_three_view_missing_side();
    test_can_generate_three_view_missing_back();
    test_can_generate_front_only_with_front();
    test_can_generate_front_only_empty();
    test_reset();
    test_app_config_defaults();
    test_app_state_defaults();
    test_glfw_init();

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run << " passed ===\n";

    return (tests_passed == tests_run) ? 0 : 1;
}
