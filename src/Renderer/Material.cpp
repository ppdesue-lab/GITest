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
