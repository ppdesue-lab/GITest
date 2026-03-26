#pragma once

#include <vector>
#include <Transform.h>
#include <Renderer/Material.h>
#include <Renderer/VertexArray.h>

#include <Renderer/VertexDesc.h>

class Mesh
{
public:
    ~Mesh() = default;

    virtual void Draw(const glm::mat4& view,const glm::mat4 proj);
    
    Ref<Material> Mat;
    Transform Transfm;
    Ref<VertexArray> VertexObject = nullptr;
};

class Object3D
{
public:
    Object3D() = default;
    ~Object3D() = default;
    template<typename T>
    bool Load(const std::string& filepath);
    virtual void Draw(const glm::mat4& view,const glm::mat4 proj);
    
	std::vector<Ref<Mesh>> Meshes;
};

extern template bool Object3D::Load<VertexColor>(const std::string& filepath);
extern template bool Object3D::Load<VertexNormal>(const std::string& filepath);