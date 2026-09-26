add_library(engine STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/NetworkChannel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/NetworkServer.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/WindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Render/OpenGL.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_sdl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends
)

target_link_libraries(engine PUBLIC
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_net::SDL3_net
    glad::glad
    glm::glm
)

target_link_libraries(engine PRIVATE
    assimp::assimp
    volk::volk
    Vulkan::Vulkan
    nlohmann_json::nlohmann_json
)

target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX- /utf-8>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
)