cmake_minimum_required(VERSION 3.21)
project(mycraft)

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

if(DEFINED ENV{VULKAN_SDK})
    set(_slangc_hints "$ENV{VULKAN_SDK}/Bin")
else()
    set(_slangc_hints "")
endif()

find_program(SLANGC_EXECUTABLE
    NAMES slangc slangc.exe
    HINTS ${_slangc_hints}
    REQUIRED
)
message(STATUS "slangc: ${SLANGC_EXECUTABLE}")

function(add_slang_shader_target TARGET)
    cmake_parse_arguments("SHADER" "" "NAME" "SOURCES" ${ARGN})
    set(OUT_DIR ${CMAKE_BINARY_DIR}/Debug/assets/shaders/build/${SHADER_NAME})
    file(MAKE_DIRECTORY ${OUT_DIR})

    set(VERT_SPV  ${OUT_DIR}/vert.spv)
    set(FRAG_SPV  ${OUT_DIR}/frag.spv)
    set(VERT_GLSL ${OUT_DIR}/vert.glsl)
    set(FRAG_GLSL ${OUT_DIR}/frag.glsl)

    set(SLANG_SPV
        -target spirv -profile spirv_1_4
        -emit-spirv-directly -fvk-use-entrypoint-name
        -matrix-layout-column-major)

    set(SLANG_GLSL
        -target glsl -profile glsl_460
        -matrix-layout-column-major)

    foreach(PAIR "VERT;vertMain;${VERT_SPV};${VERT_GLSL}"
                  "FRAG;fragMain;${FRAG_SPV};${FRAG_GLSL}")
        # 简化写法，展开两条命令
    endforeach()

    # Vulkan 产物
    add_custom_command(OUTPUT ${VERT_SPV}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES}
            ${SLANG_SPV} -entry vertMain -o ${VERT_SPV}
        DEPENDS ${SHADER_SOURCES} VERBATIM
        COMMENT "SPIR-V vert: ${SHADER_NAME}")
    add_custom_command(OUTPUT ${FRAG_SPV}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES}
            ${SLANG_SPV} -entry fragMain -o ${FRAG_SPV}
        DEPENDS ${SHADER_SOURCES} VERBATIM
        COMMENT "SPIR-V frag: ${SHADER_NAME}")

    # OpenGL 产物
    add_custom_command(OUTPUT ${VERT_GLSL}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES}
            ${SLANG_GLSL} -DGL_TARGET -entry vertMain -o ${VERT_GLSL}
        DEPENDS ${SHADER_SOURCES} VERBATIM
        COMMENT "GLSL vert: ${SHADER_NAME}")
    add_custom_command(OUTPUT ${FRAG_GLSL}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES}
            ${SLANG_GLSL} -DGL_TARGET -entry fragMain -o ${FRAG_GLSL}
        DEPENDS ${SHADER_SOURCES} VERBATIM
        COMMENT "GLSL frag: ${SHADER_NAME}")

    add_custom_target(${TARGET} DEPENDS
        ${VERT_SPV} ${FRAG_SPV} ${VERT_GLSL} ${FRAG_GLSL})
endfunction()

add_subdirectory(Engine)
add_subdirectory(mycraft)