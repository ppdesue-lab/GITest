#pragma once

#include "buffer.h"
#include <Renderer/ResourceHandle.h>
#include <unordered_map>

class VertexArray {
public:
    virtual ~VertexArray() = default;

    //bind and unbind
    virtual void Bind() const = 0;
    
    virtual void Unbind() const = 0;

    //set data
    virtual void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) = 0;
    virtual void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) = 0;

    //get data
    virtual const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const = 0;
    virtual const Ref<IndexBuffer>& GetIndexBuffer() const = 0;

    //create data
    static Ref<VertexArray> Create();
};

class GeometryLibrary
{
    struct GeometrySlot
    {
        Ref<VertexArray> Resource;
        std::string Name;
        uint32_t Generation = 1;
        bool Alive = false;
    };

public:
    static GeometryHandle Register(const Ref<VertexArray>& vertexArray, const std::string& name = "");
    static GeometryHandle CreateVertexArray(const std::string& name = "");
    static Ref<VertexArray> Resolve(GeometryHandle handle);
    static bool IsValid(GeometryHandle handle);
    static bool Release(GeometryHandle handle);
    static bool Release(const std::string& name);

private:
    static std::unordered_map<std::string, GeometryHandle> s_GeometryHandles;
    static std::vector<GeometrySlot> s_GeometrySlots;
    static std::vector<uint32_t> s_FreeGeometrySlots;
};
