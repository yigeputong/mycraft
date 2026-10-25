cmake_minimum_required(VERSION 3.21)

project(mycraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options(/utf-8 /W4 /WX- /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

# 输出目录统一（使用 $<CONFIG> 自动适配 Debug/Release 等，支持 Ninja Multi-Config）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

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
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    IMGUI_IMPL_VULKAN_USE_VOLK
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)

# 找 glslc -> 编译 GLSL -> 双 SPIR-V
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found")
endif()
message(STATUS "glslc: ${GLSLC_EXECUTABLE}")

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

set(SANITIZER "" CACHE STRING "启用 Sanitizer: address, undefined, thread, address+undefined")

if (SANITIZER AND MSVC)
    # 禁用 MSVC STL 的容器注解，解决 LNK2038 annotate_* 不匹配
    add_compile_definitions(
        _DISABLE_VECTOR_ANNOTATION
        _DISABLE_STRING_ANNOTATION
        _DISABLE_OPTIONAL_ANNOTATION
    )
endif()

function(add_sanitizer TARGET)
    if (NOT SANITIZER)
        return()
    endif()

    # ---- 针对 clang-cl 的特殊处理 ----
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MSVC)
        if (SANITIZER STREQUAL "address")
            # 加上 /Oy- 防止优化掉栈帧指针，保证 ASan 回溯准确
            target_compile_options(${TARGET} PRIVATE -fsanitize=address -g /Oy-)
            
            # 链接 ASan 运行时
            target_link_options(${TARGET} PRIVATE /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
            target_link_libraries(${TARGET} PRIVATE clang_rt.asan_dynamic-x86_64.lib)
            
            # 获取 clang 编译器的路径，以此去推断 LLVM 安装目录下的 dll 位置
            get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            file(GLOB ASAN_DLL_PATH "${LLVM_BIN_DIR}/../lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
            
            if (ASAN_DLL_PATH)
                list(GET ASAN_DLL_PATH 0 ASAN_DLL)
                add_custom_command(TARGET ${TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${ASAN_DLL}"
                        "$<TARGET_FILE_DIR:${TARGET}>/"
                    COMMENT "Copying ASan runtime DLL for clang-cl"
                )
            else()
                message(WARNING "Could not find clang_rt.asan_dynamic-x86_64.dll in LLVM directory!")
            endif()
        endif()
        return()
    endif()

    # ---- MSVC 处理 ----
    if (MSVC)
        if (SANITIZER STREQUAL "address")
            # /Oy- 防止优化掉栈帧
            target_compile_options(${TARGET} PRIVATE /fsanitize=address /Oy-)
            target_link_options(${TARGET} PRIVATE /INCREMENTAL:NO)
        endif()
        return()
    endif()

    # ---- GCC/Clang (Linux) 处理 ----
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if (SANITIZER STREQUAL "thread")
            target_compile_options(${TARGET} PRIVATE -fsanitize=thread -g -fno-omit-frame-pointer)
            target_link_options(${TARGET} PRIVATE -fsanitize=thread)
        elseif (SANITIZER STREQUAL "address+undefined")
            target_compile_options(${TARGET} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -g -fno-omit-frame-pointer)
            target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined)
        elseif (SANITIZER STREQUAL "address")
            target_compile_options(${TARGET} PRIVATE -fsanitize=address -g -fno-omit-frame-pointer)
            target_link_options(${TARGET} PRIVATE -fsanitize=address)
        endif()
    endif()
endfunction()

add_subdirectory(Engine)
add_subdirectory(mycraft)