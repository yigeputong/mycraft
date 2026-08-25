#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace mycraft {


class GLShader {
public:
    GLShader();
    ~GLShader();

    void Compile(const char* vertexPath, const char* fragmentPath);

    GLuint& get();

    void setInt(const char* name, const int value) const;
    void setFloat(const char* name, const float value) const;
    void setVec3(const char* name, const float x, const float y, const float z) const;
    void setVec3(const char* name, const glm::vec3& value) const;
    void setMat4(const char* name, const float* p) const;

private:

    GLuint program;
};

    
} // namespace mycraft
