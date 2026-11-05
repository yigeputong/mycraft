# ============================================================
# engine-core：日志 / 平台 / 配置
# ============================================================
add_library(engine-core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/FileTool.cpp
)
target_link_libraries(engine-core 
    PUBLIC SDL3::SDL3
    PRIVATE project_options project_sanitizer
)
target_include_directories(engine-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 桌面端：iconv 是 SDL3 的底层依赖，在引擎层消化
# 通过 PUBLIC 传播给所有使用方（engine-net / engine-client / game-server / mycraft），业务层无需感知
if(NOT ANDROID)
    find_package(Iconv REQUIRED)
    target_link_libraries(engine-core PUBLIC Iconv::Iconv)
endif()

# Android 端：NDK bionic 不提供标准 iconv 符号
# SDL3 预编译库若依赖系统 iconv 会链接失败，此处的定义仅影响用户侧头文件，
# 真正的图标问题需通过 minSdk 或 vcpkg overlay 在更高层处理
if(ANDROID)
    target_compile_definitions(engine-core PUBLIC SDL_ICONV=1)
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tool/Window.cpp
    
    # Service
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/WindowService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/AssetService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/InputService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/OpenGLRenderService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/VulkanRenderService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/service/VMAImpl.cpp
    
    # Client (渲染大脑)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/SceneRenderer.cpp
    
    # ImGui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/imgui.cpp
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

# 专门针对 Android 的 ImGui 渲染后端与 OpenGL ES 链接
if(ANDROID)
    # 强制 ImGui 走 OpenGL ES 3.0 渲染路径
    target_compile_definitions(engine-client PRIVATE
        IMGUI_IMPL_OPENGL_ES3
        GL_GLEXT_PROTOTYPES
    )
    # GLES 3.0 核心符号（glGenSamplers / glBindSampler / glBindVertexArray 等）
    # 由 libGLESv3.so 导出，而非 libGLESv2.so
    target_link_libraries(engine-client PRIVATE GLESv3)
endif()

target_include_directories(engine-client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui/backends
)

# ASan 运行时拷贝
if(DEFINED ASAN_DLL_FILE AND NOT ANDROID)
    add_custom_command(TARGET engine-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASAN_DLL_FILE}"
            "$<TARGET_FILE_DIR:engine-client>/"
        COMMENT "Copying ASan runtime DLL to engine-client"
    )
endif()