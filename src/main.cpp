#include "Engine.h"
#include "IGame.h"
#include <iostream>

class MyGame : public Eng::IGame {
private:
    Eng::client::WindowManager* winMgr;
    Eng::client::Window* mainWin;
    Eng::client::IRenderAPI* renderer;
    Eng::client::MeshHandle m_cubeMesh;
    Eng::client::ShaderHandle m_cubeShader;
    Eng::client::TextureHandle m_cubeTexture;
    Eng::client::Material m_cubeMaterial;
    Eng::client::Model m_model;

    Eng::client::Framebuffer fbo;
    Eng::client::ShaderHandle m_fbShader;

    Eng::client::TextureHandle m_skybox;

    bool keyEvents(const SDL_Event& event);
    bool resizeEvents(const SDL_Event& event);
    bool cursorEvents(const SDL_Event& event);
    bool scrollEvents(const SDL_Event& event);

    static constexpr float zNear = 0.5f;    //近平面
    float zFar = 256.0f;                    //远平面

    //camera
    glm::mat4 model = glm::mat4(1.0);
    glm::mat4 projection = glm::mat4(1.0);

    float cameraSpeed = 0.05f;
    float cameraSensitivity = 0.01f;
    float fov = 75.0f;
    float yaw = -90.0f;
    float pitch = 0.0f;
    glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f,  3.0f);
    glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

    void updateView();
public:
    void OnStart(Eng::Engine& engine) {
        winMgr = engine.GetWindowManager();
        winMgr->CreateWindow(engine.GetConfig().windowConfig);
        mainWin = winMgr->GetMainWindow();
        if (!mainWin) return;

        Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), true);

        mainWin->onEvent = [&](const SDL_Event& e) {
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

        mainWin->onCloseRequest = [&]() {
            engine.Quit();
        };

        renderer = engine.GetRenderAPI();
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
        m_cubeTexture = renderer->CreateTexture("./assets/textures/grass_block.png");
        m_cubeMaterial.diffuse = m_cubeTexture;

        m_model = renderer->LoadModel("./assets/objects/backpack/backpack.obj");
        if (m_model.diffuseTexture == 0) {
            // 手动加载纹理
            m_model.diffuseTexture = renderer->CreateTexture("assets/objects/backpack/diffuse.jpg");
        }

        fbo = renderer->CreateFramebuffer(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight);
        if (!fbo.isValid) {
            std::cerr << "FBO creation failed!" << std::endl;
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

    void OnUpdate(Eng::Engine& engine, float deltaTime) {
        using namespace Eng::client;

        // 按 ESC 退出
        if (Input::IsKeyPressed(KeyCode::Escape)) {
            engine.Quit();
        }

        // WASD 移动
        float speed = 3.0f * deltaTime;
        glm::vec3 front = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
        glm::vec3 right = glm::normalize(glm::cross(front, cameraUp));

        if (Input::IsKeyDown(KeyCode::W)) cameraPos += front * speed;
        if (Input::IsKeyDown(KeyCode::S)) cameraPos -= front * speed;
        if (Input::IsKeyDown(KeyCode::A)) cameraPos -= right * speed;
        if (Input::IsKeyDown(KeyCode::D)) cameraPos += right * speed;
        
        if (Input::IsKeyDown(KeyCode::Space))  cameraPos.y += speed;
        if (Input::IsKeyDown(KeyCode::LShift)) cameraPos.y -= speed;

        // 鼠标视角控制（相对模式）
        glm::vec2 delta = Input::GetMouseDelta();
        yaw   += delta.x * 0.1f;
        pitch -= delta.y * 0.1f;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);

        float scroll = Input::GetScrollDelta();
        if (scroll != 0.0f) {
            fov -= scroll * 2.0f; // 向上滚缩小 FOV（拉近视野），向下滚放大
            fov = glm::clamp(fov, 30.0f, 120.0f); // 限制范围（1~120度）
        }

        // 更新 cameraFront
        glm::vec3 mousefront;
        mousefront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        mousefront.y = sin(glm::radians(pitch));
        mousefront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cameraFront = glm::normalize(mousefront);

        if (Input::IsKeyPressed(KeyCode::Escape)) {
            engine.Quit();
        }
    }

    void OnRender(Eng::Engine& engine) {
        renderer->BindFramebuffer(fbo);

        projection = glm::perspective(glm::radians(fov), mainWin->GetAspectRatio(), zNear, zFar);
        view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f));
        model = glm::scale(model, glm::vec3(1.0f, 1.0f, 1.0f));

        renderer->SetModelMatrix(model);
        renderer->SetViewMatrix(view);
        renderer->SetProjectionMatrix(projection);

        renderer->Clear();

        renderer->DrawMesh(m_model.mesh, m_cubeShader, {m_model.diffuseTexture, 0, 32.0f});
        renderer->DrawSkybox(m_skybox, view);

        winMgr->GetMainWindow()->SwapBuffers();

        model = glm::mat4(1.0);
        view = glm::mat4(1.0);
        projection = glm::mat4(1.0);



        renderer->UnbindFramebuffer();
        renderer->Clear();

        renderer->DrawFullscreenQuad(fbo.colorTexture);
    }

    void OnShutdown(Eng::Engine& engine) {
        renderer->DestroyMesh(m_cubeMesh);
        renderer->DestroyShader(m_cubeShader);
        renderer->DestroyTexture(m_cubeTexture);
        renderer->DestroyShader(m_fbShader);
        renderer->DestroyTexture(m_skybox);
    }
};

int main() {
    Eng::Engine& eng = Eng::Engine::GetInstance();

    Eng::client::WindowConfig windowCfg;
    windowCfg.apitype = Eng::client::RenderAPItype::OPENGL;
    windowCfg.title = "Mycraft v0.0.0";
    windowCfg.windowWidth = 1280;
    windowCfg.windowHeight = 720;

    Eng::EngineConfig engConfig = {
        .mode = Eng::Mode::ClientAndServer,
        .name = "Mycraft",
        .windowConfig = windowCfg
    };

    if (!eng.Init(engConfig)) {
        std::cerr << "[Engine] Failed to initialize!" << std::endl;
        std::cerr << "[Engine] Error: " << eng.GetLastError() << std::endl;
        return -1;
    }

    MyGame game;
    eng.Run(&game);

    eng.Quit();
    return 0;
}