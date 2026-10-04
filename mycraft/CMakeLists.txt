add_executable(mycraft
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/WorldGenerator.cpp
)

target_include_directories(mycraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mycraft PRIVATE engine)

# shader 编译产物的同步 target —— 每次 build 都跑
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/build
        $<TARGET_FILE_DIR:mycraft>/assets/shaders/build
    DEPENDS shader_mesh shader_sky shader_post
    COMMENT "Copying shaders to output directory"
)

# 其它 assets（纹理、模型等）走原来的 POST_BUILD
add_custom_command(TARGET mycraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:mycraft>/assets
    COMMENT "Copying assets to output directory"
)
add_glsl_shader(shader_mesh mesh)
add_glsl_shader(shader_sky sky)
add_glsl_shader(shader_post post)

# 保证 mycraft 链接在 shader 编完之后（保持现状也行）
add_dependencies(mycraft shader_mesh shader_sky shader_post)