cmake_minimum_required(VERSION 3.20)
project(vulkan_teapot LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

include(CTest)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

foreach(shader object.vert object.frag ant.comp)
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT_DIR}/${shader}.spv"
        COMMAND "${GLSLANG_VALIDATOR}" -V
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
                -o "${SHADER_OUTPUT_DIR}/${shader}.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
        VERBATIM)
    list(APPEND SHADER_BINARIES "${SHADER_OUTPUT_DIR}/${shader}.spv")
endforeach()

add_custom_target(object_shaders DEPENDS ${SHADER_BINARIES})

add_library(object_mesh STATIC src/object.c)
target_include_directories(object_mesh PUBLIC include)
target_link_libraries(object_mesh PUBLIC m)

add_executable(navmesh_tool src/navmesh_cli.c)
target_link_libraries(navmesh_tool PRIVATE object_mesh)

add_executable(vulkan_teapot
    src/main.c
    src/app.c
    src/render.c
    src/platform.c)
add_dependencies(vulkan_teapot object_shaders)
target_include_directories(vulkan_teapot PRIVATE include)
target_compile_definitions(vulkan_teapot PRIVATE
    OBJECT_SHADER_DIR="${SHADER_OUTPUT_DIR}"
    DEFAULT_OBJECT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/teapot.obj")
target_link_libraries(vulkan_teapot PRIVATE
    object_mesh Vulkan::Vulkan PkgConfig::SDL2)

if(BUILD_TESTING)
    add_executable(object_mesh_test tests/object_test.c)
    target_link_libraries(object_mesh_test PRIVATE object_mesh)
    add_test(NAME object_mesh_test COMMAND object_mesh_test
             "${CMAKE_CURRENT_SOURCE_DIR}/assets/teapot.obj"
             "${CMAKE_CURRENT_BINARY_DIR}/object_test.nav")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(object_mesh PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(navmesh_tool PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(vulkan_teapot PRIVATE -Wall -Wextra -Wpedantic)
    if(BUILD_TESTING)
        target_compile_options(object_mesh_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
