cmake_minimum_required(VERSION 3.21)

project(mycraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

find_package(slang CONFIG HINTS "$ENV{VULKAN_SDK}/lib/cmake")
find_package(Vulkan REQUIRED)

include_directories(
    include/
    lib/SDL/include/
    C:/Dev/VulkanSDK/1.4.357.0/Include
)

set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)

add_executable(mycraft
    src/main.cpp
    src/client/Window.cpp
    src/client/Render/Vulkan.cpp
)

target_link_libraries(mycraft PRIVATE SDL3::SDL3)
target_link_libraries(mycraft PRIVATE Vulkan::Vulkan)