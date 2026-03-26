#include "stdsfx.h"
#include "Texture.h"

#include <Renderer/Renderer.h>
#include <Platform/OpenGL/OpenGLTexture.h>

std::unordered_map<std::string, Ref<Texture>> TextureLibrary::s_Textures;

Ref<Texture> TextureLibrary::GetTexture(const std::string& filepath)
{
    
	auto it = s_Textures.find(filepath);
	if (it != s_Textures.end())
		return it->second;

    //load from file
	//1 create image from filepath
	auto img = Image::Load(filepath.c_str());
	if (!img) {
		ERROR("{} not exist!", filepath);
		return nullptr;//not exist
	}
	Ref<Texture> tex;

	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:
		throw std::string("not implement!");
	case Renderer::API::OpenGL:
		tex = CreateRef<OpenGLTexture>(img);

	}

	if (tex)
	{
		s_Textures[filepath] = tex;
	}

	return tex;
};