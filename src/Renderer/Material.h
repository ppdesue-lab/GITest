#pragma once

#include "glm/glm.hpp"

#include "Shader.h"
#include <Renderer/Texture.h>

class Material
{
public:
    virtual ~Material() = default;

    virtual void Bind();
    Ref<Shader> GetShader() const;

	ShaderHandle MatShader;
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
    TextureHandle AlbedoMap;
    TextureHandle NormalMap;
    TextureHandle MetallicMap;
    TextureHandle RoughnessMap;
    TextureHandle AOMap;
    int MetallicMapChannel = 0;
    int RoughnessMapChannel = 0;
    int AOMapChannel = 0;
};

class ToonMaterial : public Material
{
public:
    ToonMaterial();

    void Bind() override;
    void BindEdge(const glm::mat4& view, const glm::mat4& projection,
        const glm::mat4& model, const glm::vec2& screenSize);

    glm::vec3 Diffuse = glm::vec3(1.0f);
    glm::vec3 Ambient = glm::vec3(0.2f);
    glm::vec3 Specular = glm::vec3(0.0f);
    float SpecularPower = 1.0f;
    float Alpha = 1.0f;
    TextureHandle MainTexture;
    TextureHandle SphereTexture;
    TextureHandle ToonTexture;
    int SphereMode = 0; // 0: disabled, 1: multiply, 2: add
    bool TwoSided = false;
    bool EdgeEnabled = true;
    glm::vec4 EdgeColor = glm::vec4(0.02f, 0.02f, 0.02f, 1.0f);
    float EdgeSize = 1.0f;

private:
    ShaderHandle m_EdgeShader;
};

class MaterialMatcap : public Material
{
public:
    MaterialMatcap(const std::string& texpath=GetFilePath("../data/images/matcap.png"),const glm::vec3& color = glm::vec3(1, 0, 1));

    void Bind() override;

    glm::vec3 Color;
    std::string TexFilePath;
    TextureHandle MatcapTex;
};
