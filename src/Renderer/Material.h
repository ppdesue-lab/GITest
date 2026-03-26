#pragma once

#include "glm/glm.hpp"

#include "Shader.h"
#include <Renderer/Texture.h>

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

class MaterialMatcap : public Material
{
public:
    MaterialMatcap(const std::string& texpath="../data/images/matcap.png",const glm::vec3& color = glm::vec3(1, 0, 1));

    void Bind() {
        assert(MatShader);
        MatShader->Bind();
        MatcapTex->Bind();
        MatShader->SetInt("u_matcapTex", MatcapTex->m_RendererID);
    }
    glm::vec3 Color;
    std::string TexFilePath;
    Ref<Texture> MatcapTex;
};