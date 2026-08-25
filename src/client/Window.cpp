#include "client/Window.h"
#include "client/Render/OpenGLAPI.h"
#include "client/Render/VulkanAPI.h"
#include <fstream>
#include <iostream>
#include <vector>

namespace mycraft {

Window& Window::getInstance() {
    static Window instance;
    return instance;
}

Window::Window()
    : m_window(nullptr), m_width(800), m_height(600), m_running(false) {}

Window::~Window() {
    Shutdown();
}

bool Window::Init(const std::string& title, int width, int height, RenderAPItype apitype) {
    m_title = title;
    m_width = width;
    m_height = height;
    m_apitype = apitype;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init Error: %s", SDL_GetError());
        return false;
    }

    m_running = true;

    return getAPI();
}

bool Window::getAPI() {
    switch(m_apitype) {
    case RenderAPItype::OPENGL:
        return CreateOpenGLWindow();
    case RenderAPItype::VULKAN:
        return CreateVulkanWindow();
    default:
        return CreateOpenGLWindow();
    }
}


bool Window::CreateOpenGLWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, OpenGLAPI::api_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, OpenGLAPI::api_minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    m_window = SDL_CreateWindow(
        m_title.c_str(),
        m_width,
        m_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!m_window) {
        SDL_Log("SDL_CreateWindow Error: %s", SDL_GetError());
        return false;
    }

    glContext = SDL_GL_CreateContext(m_window);
    if (!glContext) {
        SDL_Log("OpenGL Context Error: %s", SDL_GetError());
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    m_renderapi = std::make_unique<OpenGLAPI>();
    return m_renderapi->Init(m_window, m_width, m_height);
}

bool Window::CreateVulkanWindow() {
    m_window = SDL_CreateWindow(
        m_title.c_str(),
        m_width,
        m_height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!m_window) {
        SDL_Log("SDL_CreateWindow Error: %s", SDL_GetError());
        return false;
    }

    m_renderapi = std::make_unique<VulkanAPI>();
    return m_renderapi->Init(m_window, m_width, m_height);
}

void Window::Update(float deltaTime) {
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        SDL_Log("FPS: %.1f", 1.0f / deltaTime);
    }
}

void Window::Render() {
    m_renderapi->render();
}

bool Window::HandleEvent(SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            return true;   // 请求退出
    }
    return m_renderapi->HandleEvents(event);
}

void Window::Shutdown() {
    if (glContext) {
        SDL_GL_DestroyContext(glContext);
        glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    m_running = false;
}

}