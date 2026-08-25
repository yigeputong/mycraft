#include "client/Render/OpenGLAPI.h"
#include <SDL3/SDL_video.h>
#include <stb_image.h>
#include <iostream>
#include <vector>
#include <string>
#include <assert.h>

namespace mycraft
{

bool OpenGLAPI::Init(SDL_Window* window, int width, int height) {
    m_window = window;
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cout << "Failed to initalize GLAD." << std::endl;
        return false;
    }
    
    pixelWidth = width;
    pixelHeight = height;
    GL(glViewport(0, 0, pixelWidth, pixelHeight));
    GL(glClearColor(0.125f, 0.125f, 0.125f, 0));
    glEnable(GL_DEPTH_TEST);

    prepare();

    return true;
}

void OpenGLAPI::prepare() {
    preVAO();
    preShader(shader, "./assets/shaders/vertexshader.vert", "./assets/shaders/fragmentshader.frag");
    preShader(lightShader, "./assets/shaders/light/light.vert", "./assets/shaders/light/light.frag");
    preTexture(container, "./assets/textures/container2.png");
    preTexture(container_specular, "./assets/textures/container2_specular.png");
}

void OpenGLAPI::preVAO() {
    // std::vector<float> vertices = {
    // //     ---- 位置 ----     - 纹理坐标 -
    //      0.5f,  0.5f, 0.0f,   1.0f, 1.0f,   // 右上
    //      0.5f, -0.5f, 0.0f,   1.0f, 0.0f,   // 右下
    //     -0.5f, -0.5f, 0.0f,   0.0f, 0.0f,   // 左下
    //     -0.5f,  0.5f, 0.0f,   0.0f, 1.0f    // 左上
    // };

    float vertices[] = {
        // positions          // normals           // texture coords
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f
    };


    glGenBuffers(1, &posVbo);
    glBindBuffer(GL_ARRAY_BUFFER, posVbo); //绑定
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);  //加数据（开辟显存）

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, posVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glGenVertexArrays(1, &lightVAO);
    glBindVertexArray(lightVAO);
    glBindBuffer(GL_ARRAY_BUFFER, posVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

void OpenGLAPI::preShader(GLShader& shader, const char* vertexPath, const char* fragmentPath) {
    shader.Compile(vertexPath, fragmentPath);
}

void OpenGLAPI::preTexture(GLTexture& texture, const char* path) {
    texture.read(path);
}

void OpenGLAPI::render() {
    GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));



    GL(glUseProgram(shader.get()));

    shader.setVec3("viewPos", cameraPos);
    shader.setFloat("material.shininess", 32.0f);
    shader.setVec3("dirLight.direction", -0.2f, -1.0f, -0.3f);
    shader.setVec3("dirLight.ambient", 0.05f, 0.05f, 0.05f);
    shader.setVec3("dirLight.diffuse", 0.4f, 0.4f, 0.4f);
    shader.setVec3("dirLight.specular", 0.5f, 0.5f, 0.5f);
    // point light 1
    shader.setVec3("pointLights[0].position", pointLightPositions[0]);
    shader.setVec3("pointLights[0].ambient", 0.05f, 0.05f, 0.05f);
    shader.setVec3("pointLights[0].diffuse", 0.8f, 0.8f, 0.8f);
    shader.setVec3("pointLights[0].specular", 1.0f, 1.0f, 1.0f);
    shader.setFloat("pointLights[0].constant", 1.0f);
    shader.setFloat("pointLights[0].linear", 0.09f);
    shader.setFloat("pointLights[0].quadratic", 0.032f);
    // point light 2
    shader.setVec3("pointLights[1].position", pointLightPositions[1]);
    shader.setVec3("pointLights[1].ambient", 0.05f, 0.05f, 0.05f);
    shader.setVec3("pointLights[1].diffuse", 0.8f, 0.8f, 0.8f);
    shader.setVec3("pointLights[1].specular", 1.0f, 1.0f, 1.0f);
    shader.setFloat("pointLights[1].constant", 1.0f);
    shader.setFloat("pointLights[1].linear", 0.09f);
    shader.setFloat("pointLights[1].quadratic", 0.032f);
    // point light 3
    shader.setVec3("pointLights[2].position", pointLightPositions[2]);
    shader.setVec3("pointLights[2].ambient", 0.05f, 0.05f, 0.05f);
    shader.setVec3("pointLights[2].diffuse", 0.8f, 0.8f, 0.8f);
    shader.setVec3("pointLights[2].specular", 1.0f, 1.0f, 1.0f);
    shader.setFloat("pointLights[2].constant", 1.0f);
    shader.setFloat("pointLights[2].linear", 0.09f);
    shader.setFloat("pointLights[2].quadratic", 0.032f);
    // point light 4
    shader.setVec3("pointLights[3].position", pointLightPositions[3]);
    shader.setVec3("pointLights[3].ambient", 0.05f, 0.05f, 0.05f);
    shader.setVec3("pointLights[3].diffuse", 0.8f, 0.8f, 0.8f);
    shader.setVec3("pointLights[3].specular", 1.0f, 1.0f, 1.0f);
    shader.setFloat("pointLights[3].constant", 1.0f);
    shader.setFloat("pointLights[3].linear", 0.09f);
    shader.setFloat("pointLights[3].quadratic", 0.032f);
    // spotLight
    shader.setVec3("spotLight.position", cameraPos);
    shader.setVec3("spotLight.direction", cameraFront);
    shader.setVec3("spotLight.ambient", 0.0f, 0.0f, 0.0f);
    shader.setVec3("spotLight.diffuse", 1.0f, 1.0f, 1.0f);
    shader.setVec3("spotLight.specular", 1.0f, 1.0f, 1.0f);
    shader.setFloat("spotLight.constant", 1.0f);
    shader.setFloat("spotLight.linear", 0.09f);
    shader.setFloat("spotLight.quadratic", 0.032f);
    shader.setFloat("spotLight.cutOff", glm::cos(glm::radians(12.5f)));
    shader.setFloat("spotLight.outerCutOff", glm::cos(glm::radians(15.0f)));    

    // view/projection transformations
    projection = glm::perspective(glm::radians(fov), (float)pixelWidth / (float)pixelHeight, 0.1f, 100.0f);
    shader.setMat4("projection", glm::value_ptr(projection));
    shader.setMat4("view", glm::value_ptr(view));

    glActiveTexture(GL_TEXTURE0 + container.unit);
    glBindTexture(GL_TEXTURE_2D, container.get());
    glActiveTexture(GL_TEXTURE0 + container_specular.unit);
    glBindTexture(GL_TEXTURE_2D, container_specular.get());
    glBindVertexArray(vao);
    for(unsigned int i = 0; i < 10; i++) {
        model = glm::mat4(1.0);
        model = glm::translate(model, cubePositions[i]);
        float angle = 20.0f * i;
        model = glm::rotate(model, glm::radians(angle), glm::vec3(1.0f, 0.3f, 0.5f));
        shader.setMat4("model", glm::value_ptr(model));

        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glUseProgram(lightShader.get());

    for (int i = 0; i < 4; i++) {
        lightShader.setMat4("projection", glm::value_ptr(projection));
        lightShader.setMat4("view", glm::value_ptr(view));
        model = glm::mat4(1.0f);
        model = glm::translate(model, pointLightPositions[i]);
        model = glm::scale(model, glm::vec3(0.2f)); // a smaller cube
        lightShader.setMat4("model", glm::value_ptr(model));

        glBindVertexArray(lightVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    

    SDL_GL_SwapWindow(m_window);
}

bool OpenGLAPI::HandleEvents(SDL_Event& event) {
    bool shouldQuit = false;    //set true to quit
    switch (event.type) {

        case SDL_EVENT_QUIT:
            shouldQuit = true;  // 直接退出

        case SDL_EVENT_KEY_DOWN:
            if (keyEvents(event))
                shouldQuit = true; 
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (cursorEvents(event))
                shouldQuit = true; 
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (scrollEvents(event))
                shouldQuit = true;
            break;

    }
    return shouldQuit;
}

bool OpenGLAPI::keyEvents(SDL_Event& event) {
    switch (event.key.key) {

        case SDLK_ESCAPE:
            return true;
        case SDLK_W:
            cameraPos += cameraSpeed * cameraFront;
            break;
        case SDLK_S:
            cameraPos -= cameraSpeed * cameraFront;
            break;
        case SDLK_A:
            cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
            break;
        case SDLK_D:
            cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
            break;

        case SDLK_SPACE:
            cameraPos.y += cameraSpeed;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            cameraPos.y -= cameraSpeed;
        
    }
    updateView();
    return false;
}

bool OpenGLAPI::resizeEvents(SDL_Event& event) {
    pixelWidth = event.window.data1;
    pixelHeight = event.window.data2;
    glViewport(0, 0, pixelWidth, pixelHeight);
    return false;
}

bool OpenGLAPI::cursorEvents(SDL_Event& event) {
    float xoffset = event.motion.xrel;
    float yoffset = -event.motion.yrel;

    float sensitivity = 0.05;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw   += xoffset;
    pitch += yoffset;

    if(pitch > 89.0f)
        pitch = 89.0f;
    if(pitch < -89.0f)
        pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);

    updateView();

    return false;
}

bool OpenGLAPI::scrollEvents(SDL_Event& event) {
    float xoffset = event.wheel.x;  // 水平滚动（向右为正）
    float yoffset = event.wheel.y;  // 垂直滚动（向上为正）

    if (fov >= 1.0f && fov <= 90.0f)
        fov -= yoffset;
    if (fov < 1.0f)
        fov = 1.0f;
    if (fov > 90.0f)
        fov = 90.0f;

    return false;
}

void OpenGLAPI::updateView() {
    view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
}

void OpenGLAPI::glCheckErr() {
    GLenum errorCode = glGetError();

    std::string error = "";

    if (errorCode != GL_NO_ERROR) {
        switch (errorCode) {
        case GL_INVALID_VALUE:
            error = "INVALID_VALUE";
            break;
        case GL_INVALID_ENUM:
            error = "INVALID_ENUM";
            break;
        case GL_INVALID_OPERATION:
            error = "INVALID_OPERATION";
            break;
        case GL_OUT_OF_MEMORY:
            error = "OUT_OF_MEMORY";
            break;
        default:
            error = "UNKNOWN_ERROR";
            break;
        }
        std::cout << error << std::endl;
        assert(false);
    }
}

OpenGLAPI::~OpenGLAPI() {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &posVbo);
    glDeleteBuffers(1, &colorVbo);
    glDeleteBuffers(1, &uvVbo);
    glDeleteBuffers(1, &ebo);
}
    
} // namespace mycraft
