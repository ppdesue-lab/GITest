#pragma once

#include <vector>
#include <Transform.h>
#include <Renderer/Material.h>
#include <Renderer/VertexArray.h>


struct VertexBase
{
	glm::vec3 Position;
};

struct VertexColor : public VertexBase
{
	glm::vec3 Color;
};

struct VertexTexture : public VertexBase
{
    glm::vec2 TexCoord;
};

struct VertexNormal : public VertexBase
{
    glm::vec3 Normal;
};

struct VertexNormalTexture : public VertexBase
{
    glm::vec3 Normal;
    glm::vec2 TexCoord;
};


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