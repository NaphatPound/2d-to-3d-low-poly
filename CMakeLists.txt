cmake_minimum_required(VERSION 3.20)
project(2dto3dlowpoly VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages provided by vcpkg
find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(Stb REQUIRED)

# ONNX Runtime (installed via homebrew)
find_package(onnxruntime CONFIG REQUIRED)

# Shared app logic library (used by main and tests)
add_library(app_lib STATIC
    src/app.cpp
    src/viewport.cpp
    src/pipeline.cpp
    src/depth.cpp
    src/stb_impl.cpp
    third_party/xatlas.cpp
)
target_include_directories(app_lib PUBLIC ${Stb_INCLUDE_DIR})
target_link_libraries(app_lib PUBLIC
    imgui::imgui
    glfw
    OpenGL::GL
    nfd::nfd
    onnxruntime::onnxruntime
)
if(APPLE)
    target_link_libraries(app_lib PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# Main executable
add_executable(2DTo3DLowPoly
    src/main.cpp
)
target_link_libraries(2DTo3DLowPoly PRIVATE app_lib)

# Tests
enable_testing()
add_executable(test_app tests/test_app.cpp)
target_link_libraries(test_app PRIVATE app_lib)
add_test(NAME app_tests COMMAND test_app)
