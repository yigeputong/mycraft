cmake_minimum_required(VERSION 3.21)

project(mycraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include_directories(
    include/
    lib/imgui/
    lib/imgui/backends/
)

add_executable(mycraft
    src/main.cpp
    src/Engine.cpp

    src/core/Log.cpp

    src/client/Input.cpp
    src/client/Window.cpp
    src/client/WindowManager.cpp

    src/client/Render/OpenGL.cpp

    lib/imgui/imgui.cpp
    lib/imgui/imgui_demo.cpp 
    lib/imgui/imgui_draw.cpp 
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/backends/imgui_impl_sdl3.cpp
    lib/imgui/backends/imgui_impl_opengl3.cpp
)

find_package(SDL3_image CONFIG REQUIRED)

target_link_libraries(mycraft PRIVATE 
    SDL3::SDL3 
    SDL3_image::SDL3_image 
    assimp::assimp 
    glad::glad 
    volk::volk 
    Vulkan::Vulkan
    nlohmann_json::nlohmann_json
)

target_compile_options(mycraft PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX- /utf-8>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
)

add_custom_command(TARGET mycraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:mycraft>/assets
    COMMENT "Copying assets to output directory"
)