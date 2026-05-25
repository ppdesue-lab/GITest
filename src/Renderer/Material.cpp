#include "stdsfx.h"
#include "Material.h"

#include "Application.h"

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
    if (AlbedoMap) AlbedoMap->Bind(6);
    if (NormalMap) NormalMap->Bind(7);
    if (MetallicMap) MetallicMap->Bind(8);
    if (RoughnessMap) RoughnessMap->Bind(9);
    if (AOMap) AOMap->Bind(10);
}

MaterialMatcap::MaterialMatcap(const std::string& texpath, const glm::vec3& color)
    :Material(), Color(color),TexFilePath(texpath)
{
    //load texture 
    MatcapTex = TextureLibrary::GetTexture(texpath);
    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultMatcap");
};
