cmake_minimum_required(VERSION 3.21)
project(mycraft VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options(/utf-8 /W4 /WX- /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

# 依赖
find_package(SDL3                  CONFIG REQUIRED)
find_package(SDL3_image            CONFIG REQUIRED)
find_package(SDL3_net              CONFIG REQUIRED)
find_package(assimp                CONFIG REQUIRED)
find_package(glad                  CONFIG REQUIRED)
find_package(glm                   CONFIG REQUIRED)
find_package(volk                  CONFIG REQUIRED)
find_package(Vulkan                       REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(nlohmann_json         CONFIG REQUIRED)

add_compile_definitions(
    GLM_FORCE_DEPTH_ZERO_TO_ONE      # glm::perspective 生成 [0,1] 深度
    IMGUI_IMPL_VULKAN_USE_VOLK       # ImGui Vulkan 后端走 volk（以后用）
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)

# 找 glslc
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found")
endif()
message(STATUS "glslc: ${GLSLC_EXECUTABLE}")

# 通用函数：编译 GLSL → 双 SPIR-V
function(add_glsl_shader TARGET NAME)
    set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)
    set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/build/${NAME})
    file(MAKE_DIRECTORY ${OUT_DIR})

    set(VERT_GLSL ${SRC_DIR}/${NAME}.vert)
    set(FRAG_GLSL ${SRC_DIR}/${NAME}.frag)
    set(VERT_VK   ${OUT_DIR}/vert.vk.spv)
    set(FRAG_VK   ${OUT_DIR}/frag.vk.spv)
    set(VERT_GL   ${OUT_DIR}/vert.gl.spv)
    set(FRAG_GL   ${OUT_DIR}/frag.gl.spv)

    # Vulkan 版
    add_custom_command(OUTPUT ${VERT_VK}
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2
                ${VERT_GLSL} -o ${VERT_VK}
        DEPENDS ${VERT_GLSL} VERBATIM
        COMMENT "VK vert: ${NAME}")
    add_custom_command(OUTPUT ${FRAG_VK}
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2
                ${FRAG_GLSL} -o ${FRAG_VK}
        DEPENDS ${FRAG_GLSL} VERBATIM
        COMMENT "VK frag: ${NAME}")

    # OpenGL 版
    add_custom_command(OUTPUT ${VERT_GL}
        COMMAND ${GLSLC_EXECUTABLE} -DUSE_OPENGL --target-env=opengl4.5
                ${VERT_GLSL} -o ${VERT_GL}
        DEPENDS ${VERT_GLSL} VERBATIM
        COMMENT "GL vert: ${NAME}")
    add_custom_command(OUTPUT ${FRAG_GL}
        COMMAND ${GLSLC_EXECUTABLE} -DUSE_OPENGL --target-env=opengl4.5
                ${FRAG_GLSL} -o ${FRAG_GL}
        DEPENDS ${FRAG_GLSL} VERBATIM
        COMMENT "GL frag: ${NAME}")

    add_custom_target(${TARGET} DEPENDS
        ${VERT_VK} ${FRAG_VK} ${VERT_GL} ${FRAG_GL})
endfunction()

add_subdirectory(Engine)
add_subdirectory(mycraft)