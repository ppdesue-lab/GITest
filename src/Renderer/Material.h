#pragma once

#include "glm/glm.hpp"

#include "Shader.h"

class Material
{
public:
    ~Material() = default;

    virtual void Bind() {
        assert(MatShader);
        MatShader->Bind(); 
    };

	Ref<Shader> MatShader;
};

class MaterialColor : public Material
{
public:
    MaterialColor(const glm::vec3& color=glm::vec3(1,0,1));

    glm::vec3 Color;

};

class MaterialPhong : public Material
{
public:
    MaterialPhong(const glm::vec3& color = glm::vec3(1, 0, 1));

    glm::vec3 Color;
};