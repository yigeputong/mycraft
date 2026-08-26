#pragma once

#include <vector>
#include <string>
#include <glad/glad.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "client/Render/OpenGL/Shader.h"
#include "client/Render/OpenGL/Mesh.h"

namespace mycraft {

class GLModel {
public:
    GLModel(const char* path);
    void Draw(GLShader& shader);   
private:
    /*  模型数据  */
    std::vector<GLMesh> meshes;
    std::string directory;
    std::vector<Texture> textures_loaded;
    /*  函数   */
    void loadModel(std::string path);
    void processNode(aiNode *node, const aiScene *scene);
    GLMesh processMesh(aiMesh *mesh, const aiScene *scene);
    std::vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, std::string typeName);
};

}