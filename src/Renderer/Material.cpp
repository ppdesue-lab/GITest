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

MaterialMatcap::MaterialMatcap(const std::string& texpath, const glm::vec3& color)
    :Material(), Color(color),TexFilePath(texpath)
{
    //load texture 
    MatcapTex = TextureLibrary::GetTexture(texpath);
    MatShader = Application::Get().GetShaderLibrary()->Get("DefaultMatcap");
};
