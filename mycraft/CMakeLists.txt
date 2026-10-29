# ============================================================
# game-server 静态库（server.cpp + WorldGenerator.cpp）
# 被 mycraft 客户端 和 mycraft-server 服务端共用
# ============================================================
add_library(game-server STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/WorldGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/WorldSave.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/LightEngine.cpp
)
target_include_directories(game-server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(game-server PUBLIC engine-core engine-net)
target_link_libraries(game-server PRIVATE nlohmann_json::nlohmann_json)

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
target_link_libraries(mycraft PRIVATE game-server engine-client)
target_compile_definitions(mycraft PRIVATE MYCRAFT_VERSION="${PROJECT_VERSION}")
add_sanitizer(mycraft)

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
add_sanitizer(mycraft-server)

# ============================================================
# Shader 编译（只挂到客户端，服务端不需要）
# ============================================================
function(add_post_shader TARGET NAME)
    set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)
    set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/build/${NAME})
    file(MAKE_DIRECTORY ${OUT_DIR})

    # ★ 复用 post.vert 作为全屏顶点着色器
    set(VERT_GLSL ${SRC_DIR}/post.vert)
    set(FRAG_GLSL ${SRC_DIR}/${NAME}.frag)
    
    set(VERT_VK   ${OUT_DIR}/vert.vk.spv)
    set(FRAG_VK   ${OUT_DIR}/frag.vk.spv)
    set(VERT_GL   ${OUT_DIR}/vert.gl.spv)
    set(FRAG_GL   ${OUT_DIR}/frag.gl.spv)

    # Vulkan 版
    add_custom_command(OUTPUT ${VERT_VK} COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${VERT_GLSL} -o ${VERT_VK} DEPENDS ${VERT_GLSL} VERBATIM)
    add_custom_command(OUTPUT ${FRAG_VK} COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${FRAG_GLSL} -o ${FRAG_VK} DEPENDS ${FRAG_GLSL} VERBATIM)

    # OpenGL 版
    add_custom_command(OUTPUT ${VERT_GL} COMMAND ${GLSLC_EXECUTABLE} -DUSE_OPENGL --target-env=opengl4.5 ${VERT_GLSL} -o ${VERT_GL} DEPENDS ${VERT_GLSL} VERBATIM)
    add_custom_command(OUTPUT ${FRAG_GL} COMMAND ${GLSLC_EXECUTABLE} -DUSE_OPENGL --target-env=opengl4.5 ${FRAG_GLSL} -o ${FRAG_GL} DEPENDS ${FRAG_GLSL} VERBATIM)

    add_custom_target(${TARGET} DEPENDS ${VERT_VK} ${FRAG_VK} ${VERT_GL} ${FRAG_GL})
endfunction()

# 常规 3D 渲染 Shader
add_glsl_shader(shader_mesh mesh)
add_glsl_shader(shader_sky  sky)
add_glsl_shader(shader_post post)
add_glsl_shader(shader_lod  lod)

# 后处理 Shader
add_post_shader(shader_bloom_extract bloom_extract)
add_post_shader(shader_blur blur)
add_post_shader(shader_composite composite)

# 让 mycraft 依赖所有 shader 编译完成
add_dependencies(mycraft 
    shader_mesh shader_sky shader_post shader_lod 
    shader_bloom_extract shader_blur shader_composite
)

# ============================================================
# assets 同步：整个 assets 目录（含已编译 shader）拷到输出目录
# ============================================================
add_custom_command(TARGET mycraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:mycraft>/assets
    COMMENT "Copying assets to output directory"
)