#include "client/Render/OpenGL/Shader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

namespace mycraft {

GLShader::GLShader() {}

void GLShader::Compile(const char* vertexPath, const char* fragmentPath) {
    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;

    vShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
    try {

        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);
        std::stringstream vShaderStream, fShaderStream;

        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();       

        vShaderFile.close();
        fShaderFile.close();

        vertexCode   = vShaderStream.str();
        fragmentCode = fShaderStream.str();     
    } catch(std::ifstream::failure e) {
        std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
    }
    const char* VSCode = vertexCode.c_str();
    const char* FSCode = fragmentCode.c_str();


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

GLuint& GLShader::get() {
    return program;
}
    
void GLShader::setInt(const char* name, const int value) const {
    glUniform1i(glGetUniformLocation(program, name), value);
}

void GLShader::setFloat(const char* name, const float value) const {
    glUniform1f(glGetUniformLocation(program, name), value);
}

void GLShader::setVec3(const char* name, const float x, const float y, const float z) const {
    glUniform3f(glGetUniformLocation(program, name), x, y, z);
}

void GLShader::setVec3(const char* name, const glm::vec3& value) const {
    glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(value));
}

void GLShader::setMat4(const char* name, const float* mat) const {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, mat);
}


GLShader::~GLShader() {
    glDeleteProgram(program);
}

    
} // namespace mycraft
