#pragma once

#include "client/Render/RenderAPI.h"
#include <glad/glad.h>

#define GL(func) func;OpenGLAPI::glCheckErr()

namespace mycraft {


class OpenGLAPI final : public IRenderAPI, public APITraits<OpenGLAPI> {
public:
    ~OpenGLAPI();
    bool Init(SDL_Window* window, int width, int height) override;
    void render() override;
    void prepare();

    static void glCheckErr();

    static constexpr int api_major = 3;
    static constexpr int api_minor = 3;
private:
    SDL_Window* m_window;

    GLuint vao = 0;
    GLuint posVbo = 0;
    GLuint colorVbo = 0;
    GLuint uvVbo = 0;
    GLuint ebo = 0;
    GLuint program = 0;

    void preVAO();
    void preShader();

};
    
}
