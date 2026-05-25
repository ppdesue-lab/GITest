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

class MaterialPBR : public Material
{
public:
    MaterialPBR(const glm::vec3& albedo = glm::vec3(0.8f, 0.1f, 0.05f),
        float metallic = 0.0f, float roughness = 0.35f);
    explicit MaterialPBR(const std::string& textureDirectory);

    void Bind() override;

    glm::vec3 Albedo;
    float Metallic = 0.0f;
    float Roughness = 0.35f;
    float AmbientOcclusion = 1.0f;
    Ref<Texture> AlbedoMap;
    Ref<Texture> NormalMap;
    Ref<Texture> MetallicMap;
    Ref<Texture> RoughnessMap;
    Ref<Texture> AOMap;
};

class MaterialMatcap : public Material
{
public:
    MaterialMatcap(const std::string& texpath=GetFilePath("../data/images/matcap.png"),const glm::vec3& color = glm::vec3(1, 0, 1));

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
