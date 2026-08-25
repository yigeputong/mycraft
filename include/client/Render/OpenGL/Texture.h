#pragma once

#include <glad/glad.h>

namespace mycraft {

class GLTexture {
public:
    GLTexture();
    ~GLTexture() = default;

    void read(const char* path);
    GLuint& get();
    unsigned int unit = 0;

private:

    static unsigned int gunit;
    GLuint texture = 0;
};

    
} // namespace mycraft
