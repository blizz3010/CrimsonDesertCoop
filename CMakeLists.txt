cmake_minimum_required(VERSION 3.20)
project(CrimsonDesertCoop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Options
option(CDCOOP_BUILD_TESTS "Build unit tests" OFF)
option(CDCOOP_USE_STEAM "Enable Steam networking (requires Steamworks SDK)" ON)

# Steamworks SDK path - user must set this
set(STEAMWORKS_SDK_PATH "" CACHE PATH "Path to Steamworks SDK")

# --------------------------------------------------------------------------
# Dependencies (fetched automatically)
# --------------------------------------------------------------------------
include(FetchContent)

# safetyhook - mid-function hooking library (same as CrimsonDesert-player-status-modifier)
FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG        v0.4.1
)
FetchContent_MakeAvailable(safetyhook)

# spdlog - fast logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
)
FetchContent_MakeAvailable(spdlog)

# nlohmann/json - config file parsing
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# imgui - overlay UI
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

# --------------------------------------------------------------------------
# Core co-op library (shared between ASI and standalone builds)
# --------------------------------------------------------------------------
add_library(cdcoop_core STATIC
    src/core/hooks.cpp
    src/core/game_structures.cpp
    src/core/memory.cpp
    src/core/config.cpp

    src/network/session.cpp
    src/network/packet.cpp
    src/network/steam_network.cpp

    src/sync/player_sync.cpp
    src/sync/enemy_sync.cpp
    src/sync/world_sync.cpp
    src/sync/animation_sync.cpp

    src/player/companion_hijack.cpp
    src/player/player_manager.cpp

    src/ui/overlay.cpp
    src/ui/imgui_impl_dx12.cpp

    # ImGui sources (compiled as part of the project)
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_include_directories(cdcoop_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(cdcoop_core PUBLIC
    safetyhook
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Steamworks SDK integration
if(CDCOOP_USE_STEAM AND STEAMWORKS_SDK_PATH)
    target_include_directories(cdcoop_core PUBLIC
        ${STEAMWORKS_SDK_PATH}/public
    )
    target_link_directories(cdcoop_core PUBLIC
        ${STEAMWORKS_SDK_PATH}/redistributable_bin/win64
    )
    target_link_libraries(cdcoop_core PUBLIC steam_api64)
    target_compile_definitions(cdcoop_core PUBLIC CDCOOP_STEAM=1)
else()
    message(WARNING "Steamworks SDK not found - building without Steam networking")
    target_compile_definitions(cdcoop_core PUBLIC CDCOOP_STEAM=0)
endif()

# Windows-specific
target_link_libraries(cdcoop_core PUBLIC
    d3d12 dxgi d3dcompiler
)

target_compile_definitions(cdcoop_core PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    CDCOOP_VERSION="${PROJECT_VERSION}"
)

# --------------------------------------------------------------------------
# ASI plugin (main deliverable - loaded by ASI loader into game process)
# --------------------------------------------------------------------------
add_library(CrimsonDesertCoop SHARED
    src/asi/dllmain.cpp
)

target_link_libraries(CrimsonDesertCoop PRIVATE cdcoop_core)

# Output as .asi instead of .dll
set_target_properties(CrimsonDesertCoop PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(CDCOOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
