#pragma once

#include "client/Render/RenderAPI.h"
#include "client/Render/OpenGL/Shader.h"
#include "client/Render/OpenGL/Texture.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

#define GL(func) func;OpenGLAPI::glCheckErr()

namespace mycraft {

class OpenGLAPI final : public IRenderAPI, public APITraits<OpenGLAPI> {
public:
    ~OpenGLAPI();
    bool Init(SDL_Window* window, int width, int height) override;
    bool HandleEvents(SDL_Event& event) override;
    void prepare();
    void render() override;

    static void glCheckErr();

    static constexpr int api_major = 3;
    static constexpr int api_minor = 3;
private:
    SDL_Window* m_window;

    GLuint vao = 0;
    GLuint lightVAO = 0;
    GLuint posVbo = 0;
    GLuint colorVbo = 0;
    GLuint uvVbo = 0;
    GLuint ebo = 0;
    GLuint texture = 0;

    GLShader lightShader;
    GLShader shader;
    GLTexture container;
    GLTexture container_specular;

    int pixelWidth;
    int pixelHeight;

    void preVAO();
    void preShader(GLShader& shader,const char* vertexPath, const char* fragmentPath);
    void preTexture(GLTexture& texture, const char* path);

    glm::mat4 model      = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    glm::vec3 cubePositions[10] = {
        glm::vec3( 0.0f,  0.0f,  0.0f),
        glm::vec3( 2.0f,  5.0f, -15.0f),
        glm::vec3(-1.5f, -2.2f, -2.5f),
        glm::vec3(-3.8f, -2.0f, -12.3f),
        glm::vec3( 2.4f, -0.4f, -3.5f),
        glm::vec3(-1.7f,  3.0f, -7.5f),
        glm::vec3( 1.3f, -2.0f, -2.5f),
        glm::vec3( 1.5f,  2.0f, -2.5f),
        glm::vec3( 1.5f,  0.2f, -1.5f),
        glm::vec3(-1.3f,  1.0f, -1.5f)
    };

    static constexpr glm::vec3 pointLightPositions[4] = {
        glm::vec3( 0.7f,  0.2f,  2.0f),
        glm::vec3( 2.3f, -3.3f, -4.0f),
        glm::vec3(-4.0f,  2.0f, -12.0f),
        glm::vec3( 0.0f,  0.0f, -3.0f)
    };

    bool keyEvents(SDL_Event& event);
    bool resizeEvents(SDL_Event& event);
    bool cursorEvents(SDL_Event& event);
    bool scrollEvents(SDL_Event& event);

    //camera
    static constexpr glm::vec3 lightPos = glm::vec3(1.2f, 1.0f, 2.0f);

    float cameraSpeed = 0.05f;
    float cameraSensitivity = 0.01f;
    float fov = 45.0f;
    float yaw = -90.0f;
    float pitch = 0.0f;
    glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f,  3.0f);
    glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

    void updateView();

};
    
}
