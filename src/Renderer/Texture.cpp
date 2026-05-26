#include "stdsfx.h"
#include "Texture.h"

#include <Renderer/Renderer.h>
#include <Platform/OpenGL/OpenGLTexture.h>
#ifdef G_DX11
#include <Platform/DX11/DX11Texture.h>
#endif

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

	return GetTexture(filepath, img);
};

Ref<Texture> TextureLibrary::GetTexture(const std::string& name, const Ref<Image>& image)
{
	auto it = s_Textures.find(name);
	if (it != s_Textures.end())
		return it->second;
	if (!image)
		return nullptr;

	Ref<Texture> tex;

	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:
		throw std::string("not implement!");
	case Renderer::API::OpenGL:
		tex = CreateRef<OpenGLTexture>(image);
		break;
#ifdef G_DX11
	case Renderer::API::DX11:
		tex = CreateRef<DX11Texture>(image);
		break;
#endif

	}

	if (tex)
	{
		tex->SourceImage = image;
		s_Textures[name] = tex;
	}

	return tex;
};
