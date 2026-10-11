# ============================================================
# game-server 静态库（server.cpp + WorldGenerator.cpp）
# 被 mycraft 客户端 和 mycraft-server 服务端共用
# ============================================================
add_library(game-server STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/WorldGenerator.cpp
)
target_include_directories(game-server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(game-server PUBLIC engine-core engine-net)

# ============================================================
# 客户端
# ============================================================
add_executable(mycraft
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp
)
target_include_directories(mycraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(mycraft PRIVATE game-server engine-client)
target_compile_definitions(mycraft PRIVATE MYCRAFT_VERSION="${PROJECT_VERSION}")

# ============================================================
# 专用服务端
# ============================================================
add_executable(mycraft-server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serverMain.cpp
)
target_include_directories(mycraft-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(mycraft-server PRIVATE game-server)

# ============================================================
# Shader 编译（只挂到客户端，服务端不需要）
# ============================================================
add_glsl_shader(shader_mesh mesh)
add_glsl_shader(shader_sky  sky)
add_glsl_shader(shader_post post)

# 让 mycraft 依赖 shader 编译完成，保证 POST_BUILD 拷 assets 时
# 源码树里的 assets/shaders/build/*.spv 已经生成
add_dependencies(mycraft shader_mesh shader_sky shader_post)

# ============================================================
# assets 同步：整个 assets 目录（含已编译 shader）拷到输出目录
# ============================================================
add_custom_command(TARGET mycraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:mycraft>/assets
    COMMENT "Copying assets to output directory"
)