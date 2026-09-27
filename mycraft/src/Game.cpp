#include "game/Game.h"

#include "Engine.h"
#include "core/Log.h"
#include "core/MessageWriter.h"
#include "game/Protocol.h"
#include "game/GameServer.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

namespace game {

void MyGame::OnStart(Eng::Engine& engine) {
    m_server = std::make_unique<game::GameServer>();
    if (!m_server->Start(25565)) {
        logError(logger, "[Game] Failed to start server");
        return;
    }

    // ★ 客户端连接（主线程，会阻塞几百毫秒）
    if (!m_client.Connect("localhost", 25565)) {
        logError(logger, "[Game] Failed to connect to server");
        return;
    }
    logInfo(logger, "[Game] Connected to server");


    Eng::client::WindowConfig windowConfig = {
        .apitype = Eng::client::RenderAPItype::OPENGL,
        .title = "Mycraft v0.0.0",
        .windowWidth = 1280,
        .windowHeight = 720
    };
    winMgr = engine.GetWindowManager();
    m_win = winMgr->CreateWindow(windowConfig);
    mainWin = winMgr->GetWindow(m_win);
    if (!mainWin) return;

    Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplSDL3_InitForOpenGL(mainWin->GetSDLWindow(), mainWin->GetGLContext())) {
        logError(logger, "ImGui_ImplSDL3_InitForOpenGL failed!");
        return;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        logError(logger, "ImGui_ImplOpenGL3_Init failed!");
        return;
    }

    mainWin->onEvent = [&](const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard || io.WantCaptureMouse) {
        }
        Eng::client::Input::Get().ProcessEvent(e);
        switch(e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (e.key.key == SDLK_ESCAPE) {
                    engine.Stop();
                    break;
                }
        }
    };

    renderer = mainWin->GetAPI();
    renderer->Initialize(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight, mainWin);
    renderer->SetClearColor(0.125f,0.125f,0.125f,0.0f);

    std::vector<Eng::client::Vertex> vertices = {
        // ===== 后面 (z = -0.5) =====
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},

        // ===== 前面 (z = 0.5) =====
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {0.0f, 0.0f}},

        // ===== 左面 (x = -0.5) =====
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},

        // ===== 右面 (x = 0.5) =====
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},

        // ===== 底面 (y = -0.5) =====
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

        // ===== 顶面 (y = 0.5) =====
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}
    };

    // 索引（每个三角形三个点，共 12 个三角形）
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < 36; ++i) indices.push_back(i);
    Eng::client::MeshData data = {vertices, indices};

    m_cubeMesh = renderer->CreateMesh(data);
    m_cubeShader = renderer->CreateShader("./assets/shaders/OpenGL/model/model.vert", "./assets/shaders/OpenGL/model/model.frag");
    m_cubeTexture = renderer->CreateTexture("./assets/textures/stone.png");
    m_cubeMaterial.diffuse = m_cubeTexture;

    m_model = renderer->LoadModel("./assets/objects/testBlock0.obj");
    m_model.subMeshes[0].diffuseTexture = m_cubeTexture;

    constexpr int SIZE = 16;
    for (int x = 0; x < SIZE; ++x) {
        for (int z = 0; z < SIZE; ++z) {
            SceneObject obj;
            obj.model = m_model;
            obj.position = glm::vec3(x, 0.0f, z);
            obj.scale = glm::vec3(1.0f);
            m_objects.push_back(obj);
        }
    }

    fbo = renderer->CreateFramebuffer(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight);
    if (!fbo.isValid) {
        logError(logger, "FBO creation failed!");
    }
    std::vector<std::string> skyboxFaces = {
        "./assets/textures/skybox/right.jpg",
        "./assets/textures/skybox/left.jpg",
        "./assets/textures/skybox/top.jpg",
        "./assets/textures/skybox/bottom.jpg",
        "./assets/textures/skybox/front.jpg",
        "./assets/textures/skybox/back.jpg"
    };
    m_skybox = renderer->CreateSkybox(skyboxFaces);
}

bool MyGame::OnUpdate(Eng::Engine& engine, float deltaTime) {
    using namespace Eng::client;

    // ==================== 按 ESC 退出 ====================
    if (Input::IsKeyPressed(KeyCode::Escape)) {
        return true;
    }

    // ==================== F11 全屏切换 ====================
    if (Input::IsKeyPressed(KeyCode::F11)) {
        auto& configs = mainWin->GetConfigs();
        configs.fullscreen = !configs.fullscreen;
        mainWin->SetFullscreen(configs.fullscreen);
        fbo = renderer->CreateFramebuffer(
            mainWin->GetConfigs().windowPixelWidth,
            mainWin->GetConfigs().windowPixelHeight);
    }

    // ==================== 鼠标视角 ====================
    glm::vec2 mouseDelta = Input::GetMouseDelta();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), true);
        yaw   += mouseDelta.x * 0.1f;
        pitch -= mouseDelta.y * 0.1f;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    } else {
        Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), false);
    }

    // 滚轮 FOV
    float scroll = Input::GetScrollDelta();
    if (scroll != 0.0f) {
        s.fov -= scroll * 2.0f;
        s.fov = glm::clamp(s.fov, 30.0f, 120.0f);
    }

    // 鼠标键盘输入
    glm::vec3 mousefront;
    mousefront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    mousefront.y = sin(glm::radians(pitch));
    mousefront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(mousefront);
    glm::vec3 forward = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
    glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    glm::vec3 moveDir = glm::vec3(0.0f);
    if (Input::IsKeyDown(KeyCode::W)) moveDir += forward;
    if (Input::IsKeyDown(KeyCode::S)) moveDir -= forward;
    if (Input::IsKeyDown(KeyCode::D)) moveDir += right;
    if (Input::IsKeyDown(KeyCode::A)) moveDir -= right;
    if (Input::IsKeyDown(KeyCode::Space))  moveDir.y += 1.0f;
    if (Input::IsKeyDown(KeyCode::LShift)) moveDir.y -= 1.0f;
    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
    }
    game::net::PlayerInput input;
    input.moveDir = moveDir;
    input.look    = glm::vec2(glm::radians(yaw), glm::radians(pitch));
    input.jump    = Input::IsKeyDown(KeyCode::Space);

    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::PlayerInput));
    w.WriteVec3(input.moveDir);   // ★ vec3
    w.WriteVec2(input.look);
    w.Write(static_cast<uint8_t>(input.jump ? 1 : 0));
    m_client.Send(w.GetBuffer());
    //本地预测
    float speed = 5.0f * deltaTime;
    cameraPos += input.moveDir * speed;

    // ==================== 接收服务端状态 ====================
    std::vector<uint8_t> data;
    while (m_client.Receive(data)) {
        HandleServerMessage(data);
    }

    return false;
}

void MyGame::OnRender(Eng::Engine& engine) {

    renderer->BindFramebuffer(fbo);

        view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        projection = glm::perspective(glm::radians(s.fov), mainWin->GetAspectRatio(), zNear, s.zFar);

        renderer->SetViewMatrix(view);
        renderer->SetProjectionMatrix(projection);

    renderer->Clear();

        renderer->SetLightPosition({1.0f, 2.0f, 3.0f});
        renderer->SetViewPosition(cameraPos);

        std::vector<glm::mat4> transforms;
        transforms.reserve(m_objects.size());

        for (const auto& obj : m_objects) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, obj.position);
            model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
            model = glm::scale(model, obj.scale);
            transforms.push_back(model);
        }

        Eng::client::Material mat;
        mat.diffuse = m_model.subMeshes[0].diffuseTexture;
        renderer->DrawMeshInstanced(
            m_model.FirstMesh(),
            m_cubeShader,
            mat,
            transforms
        );

        renderer->DrawSkybox(m_skybox, view);

    renderer->UnbindFramebuffer();
    
    renderer->Clear();

        renderer->DrawFullscreenQuad(fbo.colorTexture);
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ImGui::ShowDemoWindow();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    winMgr->GetMainWindow()->SwapBuffers();
}

void MyGame::OnShutdown(Eng::Engine& engine) {
    m_client.Disconnect();
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }

    renderer->DestroyMesh(m_cubeMesh);
    renderer->DestroyShader(m_cubeShader);
    renderer->DestroyTexture(m_cubeTexture);
    renderer->DestroyShader(m_fbShader);
    renderer->DestroyTexture(m_skybox);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    winMgr->DestroyWindow(m_win);
}

void MyGame::HandleServerMessage(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::WorldState: {
            uint32_t count = r.ReadU32BE();
            for (uint32_t i = 0; i < count; ++i) {
                game::net::PlayerState ps;
                ps.id       = r.ReadU32BE();
                ps.position = r.ReadVec3();
                ps.yaw      = r.Read<float>();
                ps.pitch    = r.Read<float>();

                if (ps.id == m_myClientId) {
                    glm::vec3 serverPos = ps.position;
                    glm::vec3 diff = serverPos - cameraPos;
                    float dist = glm::length(diff);

                    if (dist > 2.0f) {
                        // 偏差太大，直接校正（瞬移）
                        cameraPos = serverPos;
                    } else if (dist > 0.1f) {
                        // 偏差小，平滑修正
                        cameraPos += diff * 0.2f;   // 每帧修正 20%
                    }
                } else {
                    // 其他玩家，存入玩家表（渲染时用）
                    m_otherPlayers[ps.id] = ps;
                }
            }
            break;
        }
        case net::MessageType::PlayerId: {  // YourId
            m_myClientId = r.ReadU32BE();
            logInfo(logger, "[Net] My client ID: " << m_myClientId);
            break;
        }
    }
}

}