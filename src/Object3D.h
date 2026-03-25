#pragma once

#include <vector>
#include <Transform.h>
#include <Renderer/Material.h>
#include <Renderer/VertexArray.h>

class Object3D
{
public:
    ~Object3D() = default;

    virtual void Draw(const glm::mat4& view,const glm::mat4 proj);
    
    Ref<Material> Mat;
    Transform Transfm;
    Ref<VertexArray> VertexObject;
};