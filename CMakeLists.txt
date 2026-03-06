cmake_minimum_required(VERSION 3.24)
project(HighPerformanceVoxelEngine VERSION 1.0)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/wd4996)
endif()

# --- Dependencies ---
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# GLAD (via libigl-glad)
FetchContent_Declare(
    libigl_glad
    URL https://github.com/libigl/libigl-glad/archive/master.zip
)
FetchContent_GetProperties(libigl_glad)
if(NOT libigl_glad_POPULATED)
    # We only need the generated glad sources/headers; don't add the upstream
    # project to avoid its CMake deprecation warnings.
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(libigl_glad)
    cmake_policy(POP)
endif()

add_library(glad STATIC
    ${libigl_glad_SOURCE_DIR}/src/glad.c
)
target_include_directories(glad PUBLIC
    ${libigl_glad_SOURCE_DIR}/include
)

# STB
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# GLM
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLM_STATIC_LIBRARY_ENABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(glm)

# EnTT
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.0
)
FetchContent_MakeAvailable(entt)

# fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)
FetchContent_MakeAvailable(fmt)

# FastNoiseLite
FetchContent_Declare(
    fast_noise_lite
    URL https://github.com/Auburn/FastNoiseLite/archive/refs/heads/master.zip
)
FetchContent_MakeAvailable(fast_noise_lite)

# --- Sources ---
# CONFIGURE_DEPENDS ensures the build system re-runs CMake when files matching
# these globs are added/removed (important for iterative development).

# Ensure source directory exists
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

file(GLOB_RECURSE CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/*.cpp"
)

file(GLOB_RECURSE HEADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/*.h"
)

if(CPP_SOURCES STREQUAL "CPP_SOURCES-NOTFOUND")
    set(CPP_SOURCES "")
endif()

if(HEADER_SOURCES STREQUAL "HEADER_SOURCES-NOTFOUND")
    set(HEADER_SOURCES "")
endif()

if(NOT CPP_SOURCES)
    message(WARNING "No .cpp source files found in src/ or vendor/imgui/. Creating a minimal main.cpp stub.")
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" "int main() { return 0; }\n")
    list(APPEND CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
endif()

set(SOURCES ${CPP_SOURCES} ${HEADER_SOURCES})

# --- Executable ---
add_executable(${PROJECT_NAME} ${SOURCES})

# --- Runtime Assets ---
# Ensure shaders/assets are available when running the exe from the build output directory.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMENT "Copying runtime assets to output directory"
)

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    vendor
    ${libigl_glad_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
    ${fast_noise_lite_SOURCE_DIR}/Cpp
    ${entt_SOURCE_DIR}/src
    ${fmt_SOURCE_DIR}/include
)

# --- Linking ---
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glad
    fmt::fmt
    glm::glm
)

# --- Definitions ---
target_compile_definitions(${PROJECT_NAME} PRIVATE GLM_FORCE_RADIANS)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /wd4005)
endif()
