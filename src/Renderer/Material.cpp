#include "stdsfx.h"
#include "Material.h"

#include "Application.h"

#ifdef G_OPENGL
#include <glad/glad.h>
#endif

void Material::Bind()
{
    Ref<Shader> shader = GetShader();
    assert(shader);
    shader->Bind();
}

Ref<Shader> Material::GetShader() const
{
    ShaderLibrary* library = ShaderLibrary::Instance();
    return library ? library->Resolve(MatShader) : nullptr;
}

MaterialColor::MaterialColor(const glm::vec3& color) :
    Material(), Color(color) {
    
    MatShader = Application::Get().GetShaderLibrary()->GetHandle("DefaultColor");
    
}

MaterialPhong::MaterialPhong(const glm::vec3& color) :
    Material(), Color(color) {

    MatShader = Application::Get().GetShaderLibrary()->GetHandle("DefaultPhong");

}

MaterialPBR::MaterialPBR(const glm::vec3& albedo, float metallic, float roughness)
    : Material(), Albedo(albedo), Metallic(metallic), Roughness(roughness)
{
    MatShader = Application::Get().GetShaderLibrary()->GetHandle("DefaultPBR");
}

MaterialPBR::MaterialPBR(const std::string& textureDirectory)
    : MaterialPBR(glm::vec3(1.0f), 1.0f, 1.0f)
{
    const std::string separator = textureDirectory.empty() || textureDirectory.back() == '/' ||
        textureDirectory.back() == '\\' ? "" : "/";
    const std::string root = textureDirectory + separator;
    AlbedoMap = TextureLibrary::LoadTexture(root + "albedo.png");
    NormalMap = TextureLibrary::LoadTexture(root + "normal.png");
    MetallicMap = TextureLibrary::LoadTexture(root + "metallic.png");
    RoughnessMap = TextureLibrary::LoadTexture(root + "roughness.png");
    AOMap = TextureLibrary::LoadTexture(root + "ao.png");
}

void MaterialPBR::Bind()
{
    Ref<Texture> albedoMap = TextureLibrary::Resolve(AlbedoMap);
    Ref<Texture> normalMap = TextureLibrary::Resolve(NormalMap);
    Ref<Texture> metallicMap = TextureLibrary::Resolve(MetallicMap);
    Ref<Texture> roughnessMap = TextureLibrary::Resolve(RoughnessMap);
    Ref<Texture> aoMap = TextureLibrary::Resolve(AOMap);

    Material::Bind();
    Ref<Shader> shader = GetShader();
    if (!shader)
        return;
    shader->SetFloat3("u_Albedo", Albedo);
    shader->SetFloat("u_Metallic", glm::clamp(Metallic, 0.0f, 1.0f));
    shader->SetFloat("u_Roughness", glm::clamp(Roughness, 0.04f, 1.0f));
    shader->SetFloat("u_AO", glm::clamp(AmbientOcclusion, 0.0f, 1.0f));
    shader->SetInt("u_AlbedoMap", 6);
    shader->SetInt("u_NormalMap", 7);
    shader->SetInt("u_MetallicMap", 8);
    shader->SetInt("u_RoughnessMap", 9);
    shader->SetInt("u_AOMap", 10);
    shader->SetInt("u_HasAlbedoMap", albedoMap ? 1 : 0);
    shader->SetInt("u_HasNormalMap", normalMap ? 1 : 0);
    shader->SetInt("u_HasMetallicMap", metallicMap ? 1 : 0);
    shader->SetInt("u_HasRoughnessMap", roughnessMap ? 1 : 0);
    shader->SetInt("u_HasAOMap", aoMap ? 1 : 0);
    shader->SetInt("u_MetallicMapChannel", MetallicMapChannel);
    shader->SetInt("u_RoughnessMapChannel", RoughnessMapChannel);
    shader->SetInt("u_AOMapChannel", AOMapChannel);
    if (albedoMap) albedoMap->Bind(6);
    if (normalMap) normalMap->Bind(7);
    if (metallicMap) metallicMap->Bind(8);
    if (roughnessMap) roughnessMap->Bind(9);
    if (aoMap) aoMap->Bind(10);
#ifdef G_OPENGL
    // Clear unused slots so missing-map materials cannot retain textures from a previous draw.
    if (!albedoMap) glBindTextureUnit(6, 0);
    if (!normalMap) glBindTextureUnit(7, 0);
    if (!metallicMap) glBindTextureUnit(8, 0);
    if (!roughnessMap) glBindTextureUnit(9, 0);
    if (!aoMap) glBindTextureUnit(10, 0);
#endif
}

ToonMaterial::ToonMaterial()
    : Material()
{
    MatShader = Application::Get().GetShaderLibrary()->GetHandle("DefaultToon");
    m_EdgeShader = Application::Get().GetShaderLibrary()->GetHandle("DefaultToonEdge");
}

void ToonMaterial::Bind()
{
    Ref<Texture> mainTexture = TextureLibrary::Resolve(MainTexture);
    Ref<Texture> sphereTexture = TextureLibrary::Resolve(SphereTexture);
    Ref<Texture> toonTexture = TextureLibrary::Resolve(ToonTexture);

    Material::Bind();
    Ref<Shader> shader = GetShader();
    if (!shader)
        return;
    shader->SetFloat3("u_Diffuse", Diffuse);
    shader->SetFloat3("u_Ambient", Ambient);
    shader->SetFloat3("u_Specular", Specular);
    shader->SetFloat("u_SpecularPower", glm::max(SpecularPower, 1.0f));
    shader->SetFloat("u_Alpha", glm::clamp(Alpha, 0.0f, 1.0f));
    shader->SetInt("u_MainTexture", 6);
    shader->SetInt("u_SphereTexture", 7);
    shader->SetInt("u_ToonTexture", 8);
    shader->SetInt("u_HasMainTexture", mainTexture ? 1 : 0);
    shader->SetInt("u_HasSphereTexture", sphereTexture && SphereMode != 0 ? 1 : 0);
    shader->SetInt("u_SphereMode", SphereMode);
    shader->SetInt("u_HasToonTexture", toonTexture ? 1 : 0);
    if (mainTexture) mainTexture->Bind(6);
    if (sphereTexture && SphereMode != 0) sphereTexture->Bind(7);
    if (toonTexture) toonTexture->Bind(8);
#ifdef G_OPENGL
    if (!mainTexture) glBindTextureUnit(6, 0);
    if (!sphereTexture || SphereMode == 0) glBindTextureUnit(7, 0);
    if (!toonTexture) glBindTextureUnit(8, 0);
#endif
}

void ToonMaterial::BindEdge(const glm::mat4& view, const glm::mat4& projection,
    const glm::mat4& model, const glm::vec2& screenSize)
{
    Ref<Shader> edgeShader = ShaderLibrary::Instance()->Resolve(m_EdgeShader);
    if (!edgeShader)
        return;
    edgeShader->Bind();
    edgeShader->SetMat4("u_View", view);
    edgeShader->SetMat4("u_Projection", projection);
    edgeShader->SetMat4("u_Model", model);
    edgeShader->SetFloat2("u_ScreenSize", screenSize);
    edgeShader->SetFloat("u_EdgeSize", EdgeSize);
    edgeShader->SetFloat4("u_EdgeColor", EdgeColor);
}

MaterialMatcap::MaterialMatcap(const std::string& texpath, const glm::vec3& color)
    :Material(), Color(color),TexFilePath(texpath)
{
    //load texture 
    MatcapTex = TextureLibrary::LoadTexture(texpath);
    MatShader = Application::Get().GetShaderLibrary()->GetHandle("DefaultMatcap");
};

void MaterialMatcap::Bind()
{
    Material::Bind();
    Ref<Shader> shader = GetShader();
    if (!shader)
        return;
    shader->SetInt("u_matcapTex", 0);
    if (Ref<Texture> matcapTex = TextureLibrary::Resolve(MatcapTex))
        matcapTex->Bind(0);
#ifdef G_OPENGL
    else
        glBindTextureUnit(0, 0);
#endif
}
