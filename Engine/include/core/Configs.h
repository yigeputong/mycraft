#pragma once
#include <string>

namespace Eng::client {

enum class RenderAPItype {
    OPENGL,
    VULKAN,
    DIRECTX12,
    METAL
};

struct WindowConfig {
    RenderAPItype apitype = RenderAPItype::OPENGL;
    std::string   title   = "Engine Window";

    int  windowWidth      = 800;
    int  windowHeight     = 600;
    int  windowPixelWidth = windowWidth;      // 高 DPI 下与 windowWidth 不同
    int  windowPixelHeight= windowHeight;

    bool resizable            = false;
    bool fullscreen           = false;
    bool borderlessFullscreen = true;
    bool relativeMode         = false;

    bool vsync = false;   // SDL_GL_SetSwapInterval处理
};

struct RenderConfig {
    // ===== 渲染分辨率 =====
    int  renderWidth  = 0;   // 0 = 跟随窗口
    int  renderHeight = 0;

    // ===== 相机 / 后处理 =====
    float fov        = 60.0f;
    float gamma      = 2.2f;
    float anisotropy = 0.0f;

    bool enableBloom      = false;
    bool enableSSAO       = false;
    bool enableMotionBlur = false;

    // ===== 性能 / 质量 =====
    int maxFPS        = 0;      // 0 = 不限帧
    int refreshRate   = 0;      // 0 = 自动
    int shadowMapSize = 2048;
    int maxLights     = 16;

    // ===== 调试显示 =====
    bool showFPS       = true;
    bool showNormals   = false;
    bool wireframeMode = false;
};

struct AppConfig {
    WindowConfig window;
    RenderConfig render;

    // TODO：从文件加载 / 保存
    // void LoadFromJson(const std::string& path);
    // void SaveToJson(const std::string& path);
};

}