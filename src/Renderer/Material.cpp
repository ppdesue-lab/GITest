#include "stdsfx.h"
#include "Material.h"

#include "Application.h"

#ifdef G_OPENGL
#include <glad/glad.h>
#endif

MaterialColor::MaterialColor(const glm::vec3& color) :
    Material(), Color(color) {
    
    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
    
}

MaterialPhong::MaterialPhong(const glm::vec3& color) :
    Material(), Color(color) {

    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultPhong");

}

MaterialPBR::MaterialPBR(const glm::vec3& albedo, float metallic, float roughness)
    : Material(), Albedo(albedo), Metallic(metallic), Roughness(roughness)
{
    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultPBR");
}

MaterialPBR::MaterialPBR(const std::string& textureDirectory)
    : MaterialPBR(glm::vec3(1.0f), 1.0f, 1.0f)
{
    const std::string separator = textureDirectory.empty() || textureDirectory.back() == '/' ||
        textureDirectory.back() == '\\' ? "" : "/";
    const std::string root = textureDirectory + separator;
    AlbedoMap = TextureLibrary::GetTexture(root + "albedo.png");
    NormalMap = TextureLibrary::GetTexture(root + "normal.png");
    MetallicMap = TextureLibrary::GetTexture(root + "metallic.png");
    RoughnessMap = TextureLibrary::GetTexture(root + "roughness.png");
    AOMap = TextureLibrary::GetTexture(root + "ao.png");
}

void MaterialPBR::Bind()
{
    Material::Bind();
    MatShader->SetFloat3("u_Albedo", Albedo);
    MatShader->SetFloat("u_Metallic", glm::clamp(Metallic, 0.0f, 1.0f));
    MatShader->SetFloat("u_Roughness", glm::clamp(Roughness, 0.04f, 1.0f));
    MatShader->SetFloat("u_AO", glm::clamp(AmbientOcclusion, 0.0f, 1.0f));
    MatShader->SetInt("u_AlbedoMap", 6);
    MatShader->SetInt("u_NormalMap", 7);
    MatShader->SetInt("u_MetallicMap", 8);
    MatShader->SetInt("u_RoughnessMap", 9);
    MatShader->SetInt("u_AOMap", 10);
    MatShader->SetInt("u_HasAlbedoMap", AlbedoMap ? 1 : 0);
    MatShader->SetInt("u_HasNormalMap", NormalMap ? 1 : 0);
    MatShader->SetInt("u_HasMetallicMap", MetallicMap ? 1 : 0);
    MatShader->SetInt("u_HasRoughnessMap", RoughnessMap ? 1 : 0);
    MatShader->SetInt("u_HasAOMap", AOMap ? 1 : 0);
    MatShader->SetInt("u_MetallicMapChannel", MetallicMapChannel);
    MatShader->SetInt("u_RoughnessMapChannel", RoughnessMapChannel);
    MatShader->SetInt("u_AOMapChannel", AOMapChannel);
    if (AlbedoMap) AlbedoMap->Bind(6);
    if (NormalMap) NormalMap->Bind(7);
    if (MetallicMap) MetallicMap->Bind(8);
    if (RoughnessMap) RoughnessMap->Bind(9);
    if (AOMap) AOMap->Bind(10);
#ifdef G_OPENGL
    // Clear unused slots so missing-map materials cannot retain textures from a previous draw.
    if (!AlbedoMap) glBindTextureUnit(6, 0);
    if (!NormalMap) glBindTextureUnit(7, 0);
    if (!MetallicMap) glBindTextureUnit(8, 0);
    if (!RoughnessMap) glBindTextureUnit(9, 0);
    if (!AOMap) glBindTextureUnit(10, 0);
#endif
}

ToonMaterial::ToonMaterial()
    : Material()
{
    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultToon");
    m_EdgeShader = Application::Get().GetShaderLibrary()->Get("DefaultToonEdge");
}

void ToonMaterial::Bind()
{
    Material::Bind();
    MatShader->SetFloat3("u_Diffuse", Diffuse);
    MatShader->SetFloat3("u_Ambient", Ambient);
    MatShader->SetFloat3("u_Specular", Specular);
    MatShader->SetFloat("u_SpecularPower", glm::max(SpecularPower, 1.0f));
    MatShader->SetFloat("u_Alpha", glm::clamp(Alpha, 0.0f, 1.0f));
    MatShader->SetInt("u_MainTexture", 6);
    MatShader->SetInt("u_SphereTexture", 7);
    MatShader->SetInt("u_ToonTexture", 8);
    MatShader->SetInt("u_HasMainTexture", MainTexture ? 1 : 0);
    MatShader->SetInt("u_HasSphereTexture", SphereTexture && SphereMode != 0 ? 1 : 0);
    MatShader->SetInt("u_SphereMode", SphereMode);
    MatShader->SetInt("u_HasToonTexture", ToonTexture ? 1 : 0);
    if (MainTexture) MainTexture->Bind(6);
    if (SphereTexture && SphereMode != 0) SphereTexture->Bind(7);
    if (ToonTexture) ToonTexture->Bind(8);
#ifdef G_OPENGL
    if (!MainTexture) glBindTextureUnit(6, 0);
    if (!SphereTexture || SphereMode == 0) glBindTextureUnit(7, 0);
    if (!ToonTexture) glBindTextureUnit(8, 0);
#endif
}

void ToonMaterial::BindEdge(const glm::mat4& view, const glm::mat4& projection,
    const glm::mat4& model, const glm::vec2& screenSize)
{
    if (!m_EdgeShader)
        return;
    m_EdgeShader->Bind();
    m_EdgeShader->SetMat4("u_View", view);
    m_EdgeShader->SetMat4("u_Projection", projection);
    m_EdgeShader->SetMat4("u_Model", model);
    m_EdgeShader->SetFloat2("u_ScreenSize", screenSize);
    m_EdgeShader->SetFloat("u_EdgeSize", EdgeSize);
    m_EdgeShader->SetFloat4("u_EdgeColor", EdgeColor);
}

MaterialMatcap::MaterialMatcap(const std::string& texpath, const glm::vec3& color)
    :Material(), Color(color),TexFilePath(texpath)
{
    //load texture 
    MatcapTex = TextureLibrary::GetTexture(texpath);
    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultMatcap");
};
