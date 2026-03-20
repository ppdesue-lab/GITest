#include "ResourceMesh.h"
//add assimp import
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

//implment Load
bool ResourceMesh::Load(const std::string& path) {
    std::cout << "[Mesh] Loading: " << path << std::endl;
    
    // 模拟IO延迟
    //std::this_thread::sleep_for(std::chrono::milliseconds(50));
    //load mesh with assimp
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Failed to load mesh: " << path << std::endl;
        return false;
    }
    //get triangles and indices
    aiMesh* mesh = scene->mMeshes[0];
    m_vertexCount = mesh->mNumVertices;
    m_indexCount = mesh->mNumFaces * 3;
    m_gpuHandle = 0xDEADBEEF; // 模拟GPU句柄
    
    // 模拟网格数据
    m_vertexData.resize(m_vertexCount * 3 * sizeof(float));
    m_indexData.resize(m_indexCount * sizeof(uint32_t));
    
    SetState(ResourceState::Loaded);
    
    return true;
}