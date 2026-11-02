#pragma once
#include <string>

namespace Eng::tool {

    enum class RenderAPItype : std::uint8_t {
        AUTO,
        OPENGL,
        VULKAN
        //DIRECTX12   可能会做
        //METAL
    };

    struct WindowConfig {
        RenderAPItype apitype = RenderAPItype::AUTO;
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
        float zFar  = 4096.0f;
        float fov   = 75.0f;
        float gamma = 2.2f;
        float m_aoStrength  = 1.0f;   
        bool  wireframeMode = false;
        
        int   mipmapLevels = 4;           // 0=关闭, 1~4=生成到 1/2^N 的 mipmap
        bool  enableAnisotropic = true;   // 是否开启各向异性过滤
        float maxAnisotropy = 16.0f;      // 各向异性倍率上限
    };

    struct InputConfig {           // ★ 新建
        float mouseSensitivity = 0.01f;
    };


    struct AppConfig {
        WindowConfig window;
        RenderConfig render;
        InputConfig  input;
    };

}