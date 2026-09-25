cmake_minimum_required(VERSION 3.21)
project(mycraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

# 依赖
find_package(SDL3           CONFIG REQUIRED)
find_package(SDL3_image     CONFIG REQUIRED)
find_package(SDL3_net       CONFIG REQUIRED)
find_package(assimp         CONFIG REQUIRED)
find_package(glad           CONFIG REQUIRED)
find_package(glm            CONFIG REQUIRED)
find_package(volk           CONFIG REQUIRED)
find_package(Vulkan                REQUIRED)
find_package(nlohmann_json  CONFIG REQUIRED)

add_subdirectory(Engine)
add_subdirectory(mycraft)