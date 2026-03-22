# Development Log

## 2026-03-22 — Project Setup, Refactor, and Testing

### Iteration 1: Environment Setup
- **Status:** COMPLETE
- Installed `pkg-config` (required by vcpkg for glfw3 build)
- Cloned and bootstrapped vcpkg in `./vcpkg/`
- Configured CMake with vcpkg toolchain file
- Dependencies installed: `imgui` (with glfw/opengl3 bindings), `glfw3`, `stb`

### Iteration 2: Initial Build
- **Status:** COMPLETE
- `cmake --build .` succeeded — binary `2DTo3DLowPoly` produced
- Application launches an ImGui window with placeholder UI (image select buttons, poly count slider, generate button)

### Iteration 3: Code Refactor for Testability
- **Status:** COMPLETE
- Extracted shared logic from monolithic `main()` into `src/app.h` and `src/app.cpp`:
  - `AppConfig` struct — window dimensions, title, clear color
  - `AppState` struct — poly count, image paths, generation flag
  - `setup_glfw_hints()` — platform-specific OpenGL setup
  - `validate_poly_count()` — range validation [500, 20000]
  - `clamp_poly_count()` — clamps to valid range
  - `can_generate()` — checks all 3 image paths are set
- Updated `main.cpp` to use the new structs/functions
- Generate button now disabled when images aren't loaded (`ImGui::BeginDisabled`)

### Iteration 4: CMakeLists.txt Update
- **Status:** COMPLETE
- Created `app_lib` static library for shared code
- Added `test_app` executable target
- Enabled CTest with `enable_testing()` and `add_test()`

### Iteration 5: Test Creation and Execution
- **Status:** COMPLETE
- Created `tests/test_app.cpp` with 11 tests:
  - `validate_poly_count_valid` — boundary and mid-range values
  - `validate_poly_count_invalid` — below min, above max, zero, negative
  - `clamp_poly_count` — clamping at both boundaries
  - `can_generate_all_images_set` — all paths present
  - `can_generate_missing_front` — missing front image
  - `can_generate_missing_side` — missing side image
  - `can_generate_missing_back` — missing back image
  - `can_generate_all_empty` — default empty state
  - `app_config_defaults` — verify default config values
  - `app_state_defaults` — verify default state values
  - `glfw_init` — GLFW library initializes successfully
- **Result:** 11/11 tests PASSED

### Bugs Found and Fixed
- **Bug:** Generate button was always clickable even with no images loaded.
  - **Fix:** Added `can_generate()` check; button is now disabled via `ImGui::BeginDisabled()`/`EndDisabled()` when images are missing.
- **Issue:** `polyCountLimit` was a local static inside `main()`, not accessible to other systems.
  - **Fix:** Moved to `AppState.poly_count_limit` for proper state management.

### Files Modified
| File | Action |
|------|--------|
| `src/app.h` | Created — shared structs and function declarations |
| `src/app.cpp` | Created — implementation of app logic |
| `src/main.cpp` | Refactored — uses AppConfig/AppState, disabled generate button |
| `tests/test_app.cpp` | Created — 11 unit tests |
| `CMakeLists.txt` | Updated — app_lib, test target, CTest |
| `CLAUDE.md` | Created — project guidance for Claude Code |

---

**Completion Status:** API IS WORKING — Project builds, runs, and all 11 tests pass.
