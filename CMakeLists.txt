cmake_minimum_required(VERSION 3.14)
project(tiny-mcp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependency: nlohmann/json ---
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Library target ---
add_library(tiny-mcp
    src/mcp_server.cpp
)

target_include_directories(tiny-mcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tiny-mcp PUBLIC nlohmann_json::nlohmann_json)

# --- Alias for FetchContent consumers ---
add_library(tiny-mcp::tiny-mcp ALIAS tiny-mcp)
