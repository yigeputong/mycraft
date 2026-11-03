# ============================================================
# game-server 静态库
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
add_executable(mycraft
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/MeshManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/TextureAtlas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/ClientWorld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/ClientUI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/LODManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/MeshBuilderUtils.cpp
)
target_include_directories(mycraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(mycraft PRIVATE game-server engine-client project_options project_sanitizer)
target_compile_definitions(mycraft PRIVATE MYCRAFT_VERSION="${PROJECT_VERSION}")

# ASan 运行时拷贝
if(DEFINED ASAN_DLL_FILE)
    add_custom_command(TARGET mycraft POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASAN_DLL_FILE}"
            "$<TARGET_FILE_DIR:mycraft>/"
        COMMENT "Copying ASan runtime DLL to mycraft"
    )
endif()

# ============================================================
# 专用服务端
# ============================================================
add_executable(mycraft-server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serverMain.cpp
)
target_include_directories(mycraft-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(mycraft-server PRIVATE game-server project_options project_sanitizer)

# ============================================================
# Shader 编译
# ============================================================
add_glsl_shader(shader_mesh   mesh)
add_glsl_shader(shader_sky    sky)
add_glsl_shader(shader_post   post)
add_glsl_shader(shader_lod    lod)
add_glsl_shader(shader_entity entity)
add_glsl_shader(shader_water  water)

add_dependencies(mycraft 
    shader_mesh shader_sky shader_post shader_lod shader_entity shader_water
)

# ============================================================
# assets 同步
# ============================================================
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:mycraft>/assets
    COMMENT "Copying assets to output directory"
)
add_dependencies(copy_assets mycraft)
