#pragma once

#include <vector>
#include <string>
#include <glad/glad.h>
#include <client/Render/OpenGL/Shader.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace mycraft {

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 UV;
};

struct Texture {
    GLuint id;
    std::string type;
    aiString path;
};

class GLMesh {
public:

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    GLMesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures);
    void Draw(GLShader &shader);

private:

    GLuint VAO, VBO, EBO;

    void setupMesh();
};  

    
} // namespace mycraft