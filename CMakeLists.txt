cmake_minimum_required(VERSION 3.22.1)

project(mycraft VERSION 0.1.0 LANGUAGES C CXX)

# =============================================================================
# 1. 全局配置
# =============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

# =============================================================================
# 2. 全局依赖查找 (vcpkg)
# =============================================================================
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

# =============================================================================
# 3. 核心开发选项
# =============================================================================
add_library(project_options INTERFACE)

target_compile_features(project_options INTERFACE cxx_std_23)
# INTERFACE 库的属性必须带 INTERFACE_ 前缀才能向链接方传播
set_target_properties(project_options PROPERTIES INTERFACE_CXX_EXTENSIONS OFF)

target_compile_definitions(project_options INTERFACE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    IMGUI_IMPL_VULKAN_USE_VOLK
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
    $<$<CONFIG:Debug>:MYCRAFT_DEBUG>
)

# 编译器特定选项
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # ---- 纯 MSVC ----
    target_compile_options(project_options INTERFACE 
        /utf-8 /W4 /WX- /permissive- /MP
    )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # ★ 核心修复：使用前端变体来区分 clang-cl 和纯 clang
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # ---- clang-cl (仅限 Windows) ----
        # /utf-8 /W4 /permissive-：保持与 MSVC 一致的命令行风格（clang-cl 支持）
        # -fdiagnostics-format=clang：强制 LLVM 原生诊断格式
        # -fcolor-diagnostics -fansi-escape-codes：终端里带颜色
        target_compile_options(project_options INTERFACE 
            /utf-8 /W4 /WX- /permissive-
            -clang:-fdiagnostics-format=clang
            -fcolor-diagnostics
            -fansi-escape-codes
        )
    else()
        # ---- 纯 clang (Android NDK / Linux / macOS) ----
        target_compile_options(project_options INTERFACE 
            -Wall -Wextra -Wpedantic -Wshadow
            -fdiagnostics-color=always
        )
    endif()

else()
    # ---- GCC / 其他 ----
    target_compile_options(project_options INTERFACE 
        -Wall -Wextra -Wpedantic -Wshadow
        -fdiagnostics-color=always
    )
endif()

# =============================================================================
# 3.5 架构基线（SIMD 自动向量化）
# =============================================================================
include(CheckCXXCompilerFlag)

set(MYCRAFT_ARCH_BASELINE "auto" CACHE STRING
    "架构基线: auto / x86-64-v2 / armv8.2-a / off")
set_property(CACHE MYCRAFT_ARCH_BASELINE PROPERTY STRINGS auto x86-64-v2 armv8.2-a off)

if(NOT MYCRAFT_ARCH_BASELINE STREQUAL "off")

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i[3-6]86")
        # ---------- x86-64 ----------
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            # 纯 MSVC：x64 下 SSE2 默认已开启，不额外指定基线。
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            # clang-cl：通过 -clang: 前缀把原生 -march 透传给底层 clang
            target_compile_options(project_options INTERFACE -clang:-march=x86-64-v2)
        else()
            # GCC / 原生 Clang (Linux/macOS x86_64)：先探测支持再启用
            check_cxx_compiler_flag("-march=x86-64-v2" HAS_MARCH_X86_64_V2)
            if(HAS_MARCH_X86_64_V2)
                target_compile_options(project_options INTERFACE -march=x86-64-v2)
            else()
                message(WARNING "编译器不支持 -march=x86-64-v2，回退到默认基线")
            endif()
        endif()

    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64|arm64-v8a")
        # ---------- ARM64 ----------
        if(APPLE)
            # Apple Silicon：使用 -mcpu 拿到微架构级调度优化。
            target_compile_options(project_options INTERFACE -mcpu=apple-m1)
        else()
            # Android / Linux ARM64：armv8.2-a 为安全基线。
            check_cxx_compiler_flag("-march=armv8.2-a" HAS_MARCH_ARMV8_2_A)
            if(HAS_MARCH_ARMV8_2_A)
                target_compile_options(project_options INTERFACE -march=armv8.2-a)
            else()
                message(WARNING "编译器不支持 -march=armv8.2-a，回退到默认基线")
            endif()
        endif()

    else()
        message(STATUS "未识别的目标架构 ${CMAKE_SYSTEM_PROCESSOR}，跳过架构基线配置")
    endif()

endif()

# =============================================================================
# 4. Sanitizer 配置
# =============================================================================
set(SANITIZER "" CACHE STRING "启用 Sanitizer: address, undefined, thread, address+undefined")
add_library(project_sanitizer INTERFACE)

if (SANITIZER)
    message(STATUS "Sanitizer enabled: ${SANITIZER}")
    if (SANITIZER AND MSVC)
        target_compile_definitions(project_sanitizer INTERFACE
            _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION _DISABLE_OPTIONAL_ANNOTATION)
    endif()

    # 统一用前端变体判断
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if (SANITIZER STREQUAL "address")
            target_compile_options(project_sanitizer INTERFACE -fsanitize=address -g /Oy-)
            target_link_options(project_sanitizer INTERFACE /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
            target_link_libraries(project_sanitizer INTERFACE clang_rt.asan_dynamic-x86_64.lib)
            
            get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            file(GLOB ASAN_DLL_PATH "${LLVM_BIN_DIR}/../lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
            if (ASAN_DLL_PATH)
                list(GET ASAN_DLL_PATH 0 ASAN_DLL)
                set(ASAN_DLL_FILE "${ASAN_DLL}" CACHE INTERNAL "ASan DLL Path")
            endif()
        endif()
    elseif (MSVC)
        if (SANITIZER STREQUAL "address")
            target_compile_options(project_sanitizer INTERFACE /fsanitize=address /Oy-)
            target_link_options(project_sanitizer INTERFACE /INCREMENTAL:NO)
        endif()
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if (SANITIZER STREQUAL "thread")
            target_compile_options(project_sanitizer INTERFACE -fsanitize=thread -g -fno-omit-frame-pointer)
            target_link_options(project_sanitizer INTERFACE -fsanitize=thread)
        elseif (SANITIZER STREQUAL "address+undefined")
            target_compile_options(project_sanitizer INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -g -fno-omit-frame-pointer)
            target_link_options(project_sanitizer INTERFACE -fsanitize=address,undefined)
        elseif (SANITIZER STREQUAL "address")
            target_compile_options(project_sanitizer INTERFACE -fsanitize=address -g -fno-omit-frame-pointer)
            target_link_options(project_sanitizer INTERFACE -fsanitize=address)
        endif()
    endif()
endif()

# =============================================================================
# 5. Shader 编译逻辑
# =============================================================================
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{PATH}")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found")
endif()
message(STATUS "glslc: ${GLSLC_EXECUTABLE}")

function(add_glsl_shader TARGET NAME)
    # assets 位于 mycraft/ 业务层子目录，使用 PROJECT_SOURCE_DIR 显式定位，
    # 不依赖调用点的 CMAKE_CURRENT_SOURCE_DIR（原写法过于隐式）
    set(SRC_DIR ${PROJECT_SOURCE_DIR}/mycraft/assets/shaders/src)
    set(OUT_DIR ${PROJECT_SOURCE_DIR}/mycraft/assets/shaders/build/${NAME})
    file(MAKE_DIRECTORY ${OUT_DIR})

    set(VERT_GLSL ${SRC_DIR}/${NAME}.vert)
    set(FRAG_GLSL ${SRC_DIR}/${NAME}.frag)
    set(VERT_VK   ${OUT_DIR}/vert.vk.spv)
    set(FRAG_VK   ${OUT_DIR}/frag.vk.spv)
    set(VERT_GL   ${OUT_DIR}/vert.gl.spv)
    set(FRAG_GL   ${OUT_DIR}/frag.gl.spv)

    add_custom_command(OUTPUT ${VERT_VK} COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${VERT_GLSL} -o ${VERT_VK} DEPENDS ${VERT_GLSL} VERBATIM COMMENT "VK vert: ${NAME}")
    add_custom_command(OUTPUT ${FRAG_VK} COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${FRAG_GLSL} -o ${FRAG_VK} DEPENDS ${FRAG_GLSL} VERBATIM COMMENT "VK frag: ${NAME}")
    add_custom_command(OUTPUT ${VERT_GL} COMMAND ${GLSLC_EXECUTABLE} -DUSE_OPENGL --target-env=opengl4.5 ${VERT_GLSL} -o ${VERT_GL} DEPENDS ${VERT_GLSL} VERBATIM COMMENT "GL vert: ${NAME}")
    add_custom_command(OUTPUT ${FRAG_GL} COMMAND ${GLSLC_EXECUTABLE} -DUSE_OPENGL --target-env=opengl4.5 ${FRAG_GLSL} -o ${FRAG_GL} DEPENDS ${FRAG_GLSL} VERBATIM COMMENT "GL frag: ${NAME}")

    add_custom_target(${TARGET} DEPENDS ${VERT_VK} ${FRAG_VK} ${VERT_GL} ${FRAG_GL})
endfunction()

add_subdirectory(Engine)
add_subdirectory(mycraft)