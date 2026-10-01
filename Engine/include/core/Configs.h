#pragma once
#include <string>

namespace Eng {

    namespace client {

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
            float zNear = 0.5f;
            float zFar  = 1000.0f;
            float fov   = 75.0f;
            float gamma = 2.2f;
            bool  wireframeMode = false;
        };

        struct InputConfig {           // ★ 新建
            float mouseSensitivity = 0.01f;
        };
    
    }


struct AppConfig {
    client::WindowConfig window;
    client::RenderConfig render;
    client::InputConfig  input;

    // TODO：从文件加载 / 保存
    // void LoadFromJson(const std::string& path);
    // void SaveToJson(const std::string& path);
};

}