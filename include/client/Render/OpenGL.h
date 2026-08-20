#pragma once

#include "client/Render/RenderAPI.h"

namespace mycraft {

class OpenGLAPI : public IRenderAPI, public APITraits<OpenGLAPI> {
public:
    bool Init(SDL_Window* window, int width, int height) override {return true;}
    static constexpr int api_major = 4;
    static constexpr int api_minor = 6;
};
    
}
