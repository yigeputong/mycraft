#include "client/Render/OpenGL/Texture.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
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

    // 1. 使用 SDL_image 加载图片
    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        std::cout << "Failed to load texture: " << path << " Error: " << SDL_GetError() << std::endl;
        return;
    }

    // 2. 强制转换为 OpenGL 需要的 RGBA32 格式 (防止颜色顺序颠倒)
    SDL_Surface* formattedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    
    // 原始的 surface 已经没用了，立刻销毁释放内存
    SDL_DestroySurface(surface);

    if (!formattedSurface) {
        std::cout << "Failed to convert texture format: " << SDL_GetError() << std::endl;
        return;
    }

    // 3. 上传数据到 OpenGL
    int width = formattedSurface->w;
    int height = formattedSurface->h;
    unsigned char* data = (unsigned char*)formattedSurface->pixels;

    // 因为已经是 RGBA32 格式，所以直接使用 GL_RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    std::cout << "Texture loaded: " << width << "x" << height << std::endl;

    // 4. 释放 SDL 内存（重点：必须用 SDL_DestroySurface）
    SDL_DestroySurface(formattedSurface);

    unit = gunit++;
}

GLuint& GLTexture::get() {
    return texture;
}

    
} // namespace mycraft
