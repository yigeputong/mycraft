cmake_minimum_required(VERSION 3.21)

project(mycraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

find_package(SDL3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(slang CONFIG HINTS "$ENV{VULKAN_SDK}/lib/cmake")
find_package(Vulkan REQUIRED)

include_directories(
    include/
)

add_executable(mycraft
    src/main.cpp
    src/client/Window.cpp
    lib/src/stb_image.cpp
    src/client/Render/OpenGL.cpp
    src/client/Render/OpenGL/Shader.cpp
    src/client/Render/OpenGL/Texture.cpp
    src/client/Render/Vulkan.cpp
)

target_link_libraries(mycraft PRIVATE SDL3::SDL3)
target_link_libraries(mycraft PRIVATE assimp::assimp)
target_link_libraries(mycraft PRIVATE glad::glad)
target_link_libraries(mycraft PRIVATE volk::volk)
target_link_libraries(mycraft PRIVATE Vulkan::Vulkan)