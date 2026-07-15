cmake_minimum_required(VERSION 3.20)
project(vulkan_teapot LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

foreach(shader teapot.vert teapot.frag)
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT_DIR}/${shader}.spv"
        COMMAND "${GLSLANG_VALIDATOR}" -V
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
                -o "${SHADER_OUTPUT_DIR}/${shader}.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
        VERBATIM)
    list(APPEND SHADER_BINARIES "${SHADER_OUTPUT_DIR}/${shader}.spv")
endforeach()

add_custom_target(teapot_shaders DEPENDS ${SHADER_BINARIES})

add_executable(vulkan_teapot
    src/main.c
    src/app.c
    src/teapot.c)
add_dependencies(vulkan_teapot teapot_shaders)
target_include_directories(vulkan_teapot PRIVATE include)
target_compile_definitions(vulkan_teapot PRIVATE
    TEAPOT_SHADER_DIR="${SHADER_OUTPUT_DIR}"
    TEAPOT_ASSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/teapot.obj")
target_link_libraries(vulkan_teapot PRIVATE
    Vulkan::Vulkan PkgConfig::SDL2 m)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vulkan_teapot PRIVATE -Wall -Wextra -Wpedantic)
endif()
