#include "client/Render/OpenGLAPI.h"
#include <SDL3/SDL_video.h>
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

    
    GL(glViewport(0, 0, width, height));
    GL(glClearColor(0.125f, 0.125f, 0.125f, 0));

    prepare();

    return true;
}

void OpenGLAPI::prepare() {
    preVAO();
    preShader();
}

void OpenGLAPI::preVAO() {
    std::vector<float> positions = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };
    std::vector<float> colors = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    std::vector<float> uvs = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.5f, 1.0f
    };
    std::vector<unsigned int> indices = {
        0, 1, 2
    };

    glGenBuffers(1, &posVbo);
    glBindBuffer(GL_ARRAY_BUFFER, posVbo); //绑定
    glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(positions[0]), positions.data(), GL_STATIC_DRAW);  //加数据（开辟显存）
    glGenBuffers(1, &colorVbo);
    glBindBuffer(GL_ARRAY_BUFFER, colorVbo);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(colors[0]), colors.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &uvVbo);
    glBindBuffer(GL_ARRAY_BUFFER, uvVbo);
    glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(uvs[0]), uvs.data(), GL_STATIC_DRAW);


    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, posVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, colorVbo);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, uvVbo);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(indices[0]), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    glBindVertexArray(0);
}

void OpenGLAPI::preShader() {
    const char* VSCode = 
        "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec3 aColor;\n"
        "out vec3 color;\n"
        "void main() {\n"
        "    color = aColor;"
        "    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
        "}\n";
    unsigned int VS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(VS, 1, &VSCode, NULL);
    glCompileShader(VS);

    int infoLogLength = 0;
    glGetShaderiv(VS, GL_INFO_LOG_LENGTH, &infoLogLength);
    if (infoLogLength > 1) { //长度>1表示有内容
        std::vector<char> infoLog(infoLogLength);
        glGetShaderInfoLog(VS, infoLogLength, nullptr, infoLog.data());
        std::cerr << "ERROR: VERTEX SHADER COMPILATION_FAILED:\n" << infoLog.data() << "\n";
    }


    const char* FSCode = 
        "#version 330 core\n"
        "in vec3 color;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = vec4(color, 1.0f);"
        "}";
    unsigned int FS = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(FS, 1, &FSCode, NULL);
    glCompileShader(FS);

    infoLogLength = 0;
    glGetShaderiv(FS, GL_INFO_LOG_LENGTH, &infoLogLength);
    if (infoLogLength > 1) { //长度>1表示有内容
        std::vector<char> infoLog(infoLogLength);
        glGetShaderInfoLog(FS, infoLogLength, nullptr, infoLog.data());
        std::cerr << "ERROR: FRAGMENT SHADER COMPILATION_FAILED:\n" << infoLog.data() << "\n";
    }

    program = glCreateProgram();
    glAttachShader(program, VS);
    glAttachShader(program, FS);
    glLinkProgram(program);
    
    infoLogLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
    if (infoLogLength > 1) { //长度>1表示有内容
        std::vector<char> infoLog(infoLogLength);
        glGetProgramInfoLog(program, infoLogLength, nullptr, infoLog.data());
        std::cerr << "ERROR: SHADER PROGRAM LINKING FAILED:\n" << infoLog.data() << "\n";
    }
}

void OpenGLAPI::render() {
    GL(glClear(GL_COLOR_BUFFER_BIT));

    GL(glUseProgram(program));
    GL(glBindVertexArray(vao));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, 0);

    SDL_GL_SwapWindow(m_window);
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
    glDeleteProgram(program);
}
    
} // namespace mycraft
