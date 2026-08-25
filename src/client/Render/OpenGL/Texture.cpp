#include "client/Render/OpenGL/Texture.h"
#include <stb_image.h>
#include <iostream>

namespace mycraft {

unsigned int GLTexture::gunit = 0;

GLTexture::GLTexture() {}

void GLTexture::read(const char* path) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);   
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  //放大
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);   //缩小

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "Texture loaded: " << width << "x" << height << ", channels=" << nrChannels << std::endl;
    } else {
        std::cout << "Failed to load texture" << std::endl;
    }
    stbi_image_free(data);
    unit = gunit++;
}

GLuint& GLTexture::get() {
    return texture;
}

    
} // namespace mycraft
