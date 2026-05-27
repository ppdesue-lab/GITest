#pragma once

#include <vector>
#include <filesystem>
#include <Transform.h>
#include <Renderer/Material.h>
#include <Renderer/VertexArray.h>

#include <Renderer/VertexDesc.h>

struct BoundingSphere
{
    glm::vec3 Center = glm::vec3(0.0f);
    float Radius = 0.0f;
    bool Valid = false;
};

class Mesh
{
public:
    ~Mesh() = default;

    virtual void Draw(const glm::mat4& view, const glm::mat4 proj,
        const glm::mat4& parentTransform = glm::mat4(1.0f));
    void UpdateBoundingSphere();
    
    Ref<Material> Mat;
    Transform Transfm;
    Ref<VertexArray> VertexObject = nullptr;
    Ref<VertexArray> EdgeVertexObject = nullptr;
    uint32_t EdgeVertexCount = 0;
    std::vector<VertexNormalTexture> TraceVertices;
    std::vector<uint32_t> TraceIndices;
    std::vector<glm::vec3> EdgeVertices;
    bool ShowEdges = false;
    BoundingSphere Bounds;
};

class Object3D
{
public:
    Object3D() = default;
    ~Object3D() = default;
    template<typename T>
    bool Load(const std::string& filepath);
    template<typename T>
    bool LoadFromPath(const std::filesystem::path& filepath);
    virtual void Draw(const glm::mat4& view,const glm::mat4 proj);
    void UpdateBoundingSphere();
    BoundingSphere GetWorldBoundingSphere() const;

    Transform Transfm;
	std::vector<Ref<Mesh>> Meshes;
    BoundingSphere Bounds;
};

extern template bool Object3D::Load<VertexColor>(const std::string& filepath);
extern template bool Object3D::Load<VertexNormal>(const std::string& filepath);
