cmake_minimum_required(VERSION 3.21)

project(mycraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

find_package(slang CONFIG HINTS "$ENV{VULKAN_SDK}/lib/cmake")
find_package(Vulkan REQUIRED)

include_directories(
    include/
    lib/SDL/include/
    lib/include/
    ${Vulkan_INCLUDE_DIRS}
)

set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)

add_executable(mycraft
    src/main.cpp
    src/client/Window.cpp
    lib/src/glad.c
    lib/src/volk.c
    src/client/Render/OpenGL.cpp
    src/client/Render/Vulkan.cpp
)

target_link_libraries(mycraft PRIVATE SDL3::SDL3)