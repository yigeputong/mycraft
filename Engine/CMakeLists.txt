# ============================================================
# engine-core：日志 / 平台 / 配置
# 无图形、无网络，server / client 都能用
# ============================================================
add_library(engine-core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Platform.cpp
)
target_include_directories(engine-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine-core PUBLIC
    SDL3::SDL3
)

# ============================================================
# engine-net：网络（依赖 core）
# ============================================================
add_library(engine-net STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/NetworkChannel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/NetworkServer.cpp
)
target_include_directories(engine-net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine-net PUBLIC
    engine-core
    SDL3_net::SDL3_net
)

# ============================================================
# engine-client：窗口 / 输入 / 渲染 / ImGui / 主循环
# 依赖 core + net
# ============================================================
add_library(engine-client STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/WindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Render/OpenGL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Render/Vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/Render/VmaImpl.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_sdl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(engine-client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends
)
target_link_libraries(engine-client
    PUBLIC
        engine-core
        engine-net
        SDL3::SDL3
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
        glad::glad
        glm::glm
        volk::volk
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    PRIVATE
        assimp::assimp
        nlohmann_json::nlohmann_json
)