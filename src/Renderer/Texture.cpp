#include "stdsfx.h"
#include "Texture.h"

#include <Renderer/Renderer.h>
#include <Platform/OpenGL/OpenGLTexture.h>
#ifdef G_DX11
#include <Platform/DX11/DX11Texture.h>
#endif

#include <algorithm>
#include <filesystem>

std::unordered_map<std::string, TextureHandle> TextureLibrary::s_TextureHandles;
std::vector<TextureLibrary::TextureSlot> TextureLibrary::s_TextureSlots;
std::vector<uint32_t> TextureLibrary::s_FreeTextureSlots;

namespace
{
Ref<Texture> CreateTextureResource(const Ref<Image>& image)
{
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

	return tex;
}

Ref<Texture> CreateTextureResourceFromFile(const std::string& filepath)
{
	std::string extension = std::filesystem::path(filepath).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (extension != ".dds")
		return nullptr;

	std::string lowerPath = filepath;
	std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const bool srgb = false;

	switch (Renderer::GetAPI())
	{
	case Renderer::API::None:
		throw std::string("not implement!");
	case Renderer::API::OpenGL:
		return CreateRef<OpenGLTexture>(filepath, srgb);
#ifdef G_DX11
	case Renderer::API::DX11:
		ERROR("DDS TextureLibrary loading is only wired for OpenGL currently: {}", filepath);
		return nullptr;
#endif
	}

	return nullptr;
}
}

TextureHandle TextureLibrary::LoadTexture(const std::string& filepath)
{
	auto it = s_TextureHandles.find(filepath);
	if (it != s_TextureHandles.end() && IsValid(it->second))
		return it->second;

	if (Ref<Texture> ddsTexture = CreateTextureResourceFromFile(filepath))
	{
		if (ddsTexture->GetWidth() == 0 || ddsTexture->GetHeight() == 0)
			return {};

		uint32_t index = 0;
		if (!s_FreeTextureSlots.empty())
		{
			index = s_FreeTextureSlots.back();
			s_FreeTextureSlots.pop_back();
		}
		else
		{
			index = static_cast<uint32_t>(s_TextureSlots.size());
			s_TextureSlots.emplace_back();
		}

		TextureSlot& slot = s_TextureSlots[index];
		if (slot.Generation == 0)
			slot.Generation = 1;
		slot.Resource = ddsTexture;
		slot.SourceImage.reset();
		slot.Name = filepath;
		slot.Alive = true;

		TextureHandle handle;
		handle.Index = index;
		handle.Generation = slot.Generation;
		s_TextureHandles[filepath] = handle;
		return handle;
	}

    //load from file
	//1 create image from filepath
	auto img = Image::Load(filepath.c_str());
	if (!img) {
		ERROR("{} not exist!", filepath);
		return {};
	}

	return LoadTexture(filepath, img);
};

TextureHandle TextureLibrary::LoadTexture(const std::string& name, const Ref<Image>& image)
{
	auto it = s_TextureHandles.find(name);
	if (it != s_TextureHandles.end() && IsValid(it->second))
		return it->second;
	if (!image)
		return {};

	Ref<Texture> tex = CreateTextureResource(image);
	if (!tex)
		return {};

	uint32_t index = 0;
	if (!s_FreeTextureSlots.empty())
	{
		index = s_FreeTextureSlots.back();
		s_FreeTextureSlots.pop_back();
	}
	else
	{
		index = static_cast<uint32_t>(s_TextureSlots.size());
		s_TextureSlots.emplace_back();
	}

	TextureSlot& slot = s_TextureSlots[index];
	if (slot.Generation == 0)
		slot.Generation = 1;
	slot.Resource = tex;
	slot.SourceImage = image;
	slot.Name = name;
	slot.Alive = true;
	tex->SourceImage = image;

	TextureHandle handle;
	handle.Index = index;
	handle.Generation = slot.Generation;
	s_TextureHandles[name] = handle;
	return handle;
};

Ref<Texture> TextureLibrary::Resolve(TextureHandle handle)
{
	if (!handle.IsValid() || handle.Index >= s_TextureSlots.size())
		return nullptr;

	const TextureSlot& slot = s_TextureSlots[handle.Index];
	if (!slot.Alive || slot.Generation != handle.Generation)
		return nullptr;

	return slot.Resource;
}

bool TextureLibrary::IsValid(TextureHandle handle)
{
	return Resolve(handle) != nullptr;
}

bool TextureLibrary::Release(TextureHandle handle)
{
	if (!handle.IsValid() || handle.Index >= s_TextureSlots.size())
		return false;

	TextureSlot& slot = s_TextureSlots[handle.Index];
	if (!slot.Alive || slot.Generation != handle.Generation)
		return false;

	if (!slot.Name.empty())
	{
		auto it = s_TextureHandles.find(slot.Name);
		if (it != s_TextureHandles.end() && it->second == handle)
			s_TextureHandles.erase(it);
	}

	slot.Resource.reset();
	slot.SourceImage.reset();
	slot.Name.clear();
	slot.Alive = false;
	++slot.Generation;
	if (slot.Generation == 0)
		slot.Generation = 1;
	s_FreeTextureSlots.push_back(handle.Index);
	return true;
}

bool TextureLibrary::Release(const std::string& name)
{
	auto it = s_TextureHandles.find(name);
	if (it == s_TextureHandles.end())
		return false;

	return Release(it->second);
}

Ref<Texture> TextureLibrary::GetTexture(const std::string& filepath)
{
	return Resolve(LoadTexture(filepath));
}

Ref<Texture> TextureLibrary::GetTexture(const std::string& name, const Ref<Image>& image)
{
	return Resolve(LoadTexture(name, image));
}
