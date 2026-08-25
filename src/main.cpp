#define SDL_MAIN_USE_CALLBACKS  // 必须放在任何 SDL 头文件之前！

#include "client/Window.h"
#include <SDL3/SDL_main.h>

using namespace mycraft;

struct App {
    Window* g_window = nullptr;
};

// 全局对象
static App app;

// 1. 初始化回调
SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    SDL_Log("SDL_AppInit called");  // 看看这条是否打印
    Window& win = Window::getInstance();
    app.g_window = &win;

    if (!win.Init("Mycraft v0.0.0", 800, 600, Window::RenderAPItype::OPENGL)) {
        return SDL_APP_FAILURE;
    }
    
    SDL_SetWindowRelativeMouseMode(win.getWindow(), true);

    *appstate = &app;
    return SDL_APP_CONTINUE;
}

// 2. 迭代回调（每帧执行）
SDL_AppResult SDL_AppIterate(void* appstate) {
    Window* win = ((App*)appstate)->g_window;

    static Uint64 lastTime = SDL_GetTicks();
    Uint64 now = SDL_GetTicks();
    float deltaTime = (now - lastTime) / 1000.0f;
    lastTime = now;

    if (!win->isRunning()) {
        return SDL_APP_SUCCESS;
    }

    win->Update(deltaTime);
    win->Render();

    return SDL_APP_CONTINUE;
}

// 事件回调
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    Window* win = ((App*)appstate)->g_window;

    // 如果窗口类处理事件后返回 true，表示请求退出
    if (win->HandleEvent(*event)) {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

// 退出回调
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    SDL_Log("SDL_AppQuit called");
    Window* win = ((App*)appstate)->g_window;
    if (win) {
        win->Shutdown();
    }
    ((App*)appstate)->g_window = nullptr;
}