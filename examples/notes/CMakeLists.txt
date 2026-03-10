cmake_minimum_required(VERSION 3.14)
project(notes-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Fetch tiny-mcp ---
include(FetchContent)

FetchContent_Declare(
    tiny_mcp
    GIT_REPOSITORY https://github.com/jkerdels/tiny-mcp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(tiny_mcp)

# --- Build ---
add_executable(notes-server
    src/main.cpp
    src/notes.cpp
)

target_link_libraries(notes-server PRIVATE tiny-mcp::tiny-mcp)
