# ============================================================
# engine-core：日志 / 平台 / 配置
# ============================================================
add_library(engine-core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Platform.cpp
)
target_link_libraries(engine-core 
    PUBLIC SDL3::SDL3
    PRIVATE project_options project_sanitizer
)
# ★ 指向 include，这样外部写 #include "Engine/tool/xxx" 才能找到
target_include_directories(engine-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ============================================================
# engine-net：网络（依赖 core）
# ============================================================
add_library(engine-net STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/NetworkChannel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/NetworkServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/MemoryTransport.cpp
)
target_link_libraries(engine-net
    PUBLIC engine-core SDL3_net::SDL3_net
    PRIVATE project_options project_sanitizer
)
target_include_directories(engine-net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# =============================================================
# engine-client：窗口 / 输入 / 渲染 / ImGui
# =============================================================
add_library(engine-client STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Engine.cpp
    
    # Tool
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Window.cpp
    
    # Service
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/WindowService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/AssetService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/OpenGLRenderService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/VulkanRenderService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/VMAImpl.cpp
    
    # Client (渲染大脑)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/SceneRenderer.cpp
    
    # ImGui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_sdl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends/imgui_impl_vulkan.cpp
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
        project_options
        project_sanitizer
)
target_include_directories(engine-client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends
)

# ASan 运行时拷贝
if(DEFINED ASAN_DLL_FILE)
    add_custom_command(TARGET engine-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASAN_DLL_FILE}"
            "$<TARGET_FILE_DIR:engine-client>/"
        COMMENT "Copying ASan runtime DLL to engine-client"
    )
endif()