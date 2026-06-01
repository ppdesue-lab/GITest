#include "VertexArray.h"

#include <Renderer/Renderer.h>
#include <Platform/OpenGL/VertexArrayOpenGL.h>
#ifdef G_DX11
#include <Platform/DX11/DX11VertexArray.h>
#endif

std::unordered_map<std::string, GeometryHandle> GeometryLibrary::s_GeometryHandles;
std::vector<GeometryLibrary::GeometrySlot> GeometryLibrary::s_GeometrySlots;
std::vector<uint32_t> GeometryLibrary::s_FreeGeometrySlots;

//create vertexarray
Ref<VertexArray> VertexArray::Create()
{
    switch (Renderer::GetAPI())
    {
    case Renderer::API::OpenGL:
        return CreateRef<VertexArrayOpenGL>();
#ifdef G_DX11
    case Renderer::API::DX11:
        return CreateRef<DX11VertexArray>();
#endif
    }

    return nullptr;
};

GeometryHandle GeometryLibrary::Register(const Ref<VertexArray>& vertexArray, const std::string& name)
{
    if (!vertexArray)
        return {};

    if (!name.empty())
    {
        auto existing = s_GeometryHandles.find(name);
        if (existing != s_GeometryHandles.end() && IsValid(existing->second))
        {
            GeometrySlot& slot = s_GeometrySlots[existing->second.Index];
            slot.Resource = vertexArray;
            slot.Name = name;
            slot.Alive = true;
            return existing->second;
        }
    }

    uint32_t index = 0;
    if (!s_FreeGeometrySlots.empty())
    {
        index = s_FreeGeometrySlots.back();
        s_FreeGeometrySlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(s_GeometrySlots.size());
        s_GeometrySlots.emplace_back();
    }

    GeometrySlot& slot = s_GeometrySlots[index];
    if (slot.Generation == 0)
        slot.Generation = 1;
    slot.Resource = vertexArray;
    slot.Name = name;
    slot.Alive = true;

    GeometryHandle handle;
    handle.Index = index;
    handle.Generation = slot.Generation;
    if (!name.empty())
        s_GeometryHandles[name] = handle;
    return handle;
}

GeometryHandle GeometryLibrary::CreateVertexArray(const std::string& name)
{
    return Register(VertexArray::Create(), name);
}

Ref<VertexArray> GeometryLibrary::Resolve(GeometryHandle handle)
{
    if (!handle.IsValid() || handle.Index >= s_GeometrySlots.size())
        return nullptr;

    const GeometrySlot& slot = s_GeometrySlots[handle.Index];
    if (!slot.Alive || slot.Generation != handle.Generation)
        return nullptr;

    return slot.Resource;
}

bool GeometryLibrary::IsValid(GeometryHandle handle)
{
    return Resolve(handle) != nullptr;
}

bool GeometryLibrary::Release(GeometryHandle handle)
{
    if (!handle.IsValid() || handle.Index >= s_GeometrySlots.size())
        return false;

    GeometrySlot& slot = s_GeometrySlots[handle.Index];
    if (!slot.Alive || slot.Generation != handle.Generation)
        return false;

    if (!slot.Name.empty())
    {
        auto it = s_GeometryHandles.find(slot.Name);
        if (it != s_GeometryHandles.end() && it->second == handle)
            s_GeometryHandles.erase(it);
    }

    slot.Resource.reset();
    slot.Name.clear();
    slot.Alive = false;
    ++slot.Generation;
    if (slot.Generation == 0)
        slot.Generation = 1;
    s_FreeGeometrySlots.push_back(handle.Index);
    return true;
}

bool GeometryLibrary::Release(const std::string& name)
{
    auto it = s_GeometryHandles.find(name);
    if (it == s_GeometryHandles.end())
        return false;

    return Release(it->second);
}
