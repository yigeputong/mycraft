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

add_slang_shader_target(slang_mesh
    NAME mesh
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src/mesh.slang)
add_slang_shader_target(slang_sky
    NAME sky
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src/sky.slang)
add_slang_shader_target(slang_post
    NAME post
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src/post.slang)

# shader 编译产物的同步 target —— 每次 build 都跑
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/build
        $<TARGET_FILE_DIR:mycraft>/assets/shaders/build
    DEPENDS slang_mesh slang_sky slang_post
    COMMENT "Copying shaders to output directory"
)

# 其它 assets（纹理、模型等）走原来的 POST_BUILD
add_custom_command(TARGET mycraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:mycraft>/assets
    COMMENT "Copying assets to output directory"
)

# 保证 mycraft 链接在 shader 编完之后（保持现状也行）
add_dependencies(mycraft slang_mesh slang_sky slang_post)