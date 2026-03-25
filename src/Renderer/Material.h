#pragma once

#include "glm/glm.hpp"

#include "Shader.h"

class Material
{
public:
    ~Material() = default;

    virtual void Bind() = 0;

	Ref<Shader> MatShader;
};

class MaterialColor : public Material
{
public:
    MaterialColor(const glm::vec3& color) :
        Material(), Color(color) {
    }

    glm::vec3 Color;

};