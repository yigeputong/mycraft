# ============================================================
# game-server 静态库 (桌面和安卓都需要)
# ============================================================
add_library(game-server STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/WorldGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/WorldSave.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/DensityFunctionJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/LightEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/BlockRegistry.cpp
)
target_include_directories(game-server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(game-server 
    PUBLIC engine-core engine-net
    PRIVATE nlohmann_json::nlohmann_json project_options project_sanitizer
)

# ============================================================
# 客户端
# ============================================================
# ★ 把要编译的源文件提出来
set(CLIENT_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/MeshManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/TextureAtlas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/ClientWorld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/ClientUI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/LODManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/MeshBuilderUtils.cpp
)

if(ANDROID)
    # Android 平台：编译成动态库，名字叫 main
    add_library(main SHARED ${CLIENT_SRCS})
    target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    # 安卓下没有 SDL3::SDL3main 这个 Target，直接链接 SDL3::SDL3 即可。
    # 安卓的入口点是通过 Java 层的 SDLActivity 调用 JNI 处理的。
    target_link_libraries(main PRIVATE 
        game-server engine-client project_options project_sanitizer 
        SDL3::SDL3 GLESv3
    )
    target_compile_definitions(main PRIVATE MYCRAFT_VERSION="${PROJECT_VERSION}")
else()
    # 桌面平台：保持原样编译成 exe
    add_executable(mycraft ${CLIENT_SRCS})
    target_include_directories(mycraft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(mycraft PRIVATE game-server engine-client project_options project_sanitizer)
    target_compile_definitions(mycraft PRIVATE MYCRAFT_VERSION="${PROJECT_VERSION}")

    # 桌面端的 ASan 运行时拷贝
    if(DEFINED ASAN_DLL_FILE)
        add_custom_command(TARGET mycraft POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ASAN_DLL_FILE}"
                "$<TARGET_FILE_DIR:mycraft>/"
            COMMENT "Copying ASan runtime DLL to mycraft"
        )
    endif()

    # 桌面端专用的独立服务端
    add_executable(mycraft-server ${CMAKE_CURRENT_SOURCE_DIR}/src/serverMain.cpp)
    target_include_directories(mycraft-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(mycraft-server PRIVATE game-server project_options project_sanitizer)
endif()

# ============================================================
# Shader 编译 (两端都需要)
# ============================================================
add_glsl_shader(shader_mesh   mesh)
add_glsl_shader(shader_sky    sky)
add_glsl_shader(shader_post   post)
add_glsl_shader(shader_lod    lod)
add_glsl_shader(shader_entity entity)
add_glsl_shader(shader_water  water)

# 如果是安卓，main 依赖 shader；如果是桌面，mycraft 依赖 shader
if(ANDROID)
    add_dependencies(main shader_mesh shader_sky shader_post shader_lod shader_entity shader_water)
else()
    add_dependencies(mycraft shader_mesh shader_sky shader_post shader_lod shader_entity shader_water)
    
    # assets 位于 mycraft/ 业务层目录，用 PROJECT_SOURCE_DIR 显式定位
    # 桌面端的 assets 同步（Android 不需要，因为 Gradle 会自动打进 apk）
    add_custom_target(copy_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/mycraft/assets
            $<TARGET_FILE_DIR:mycraft>/assets
        COMMENT "Copying assets to output directory"
    )
    add_dependencies(copy_assets mycraft)
endif()