#pragma once

#include "buffer.h"

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