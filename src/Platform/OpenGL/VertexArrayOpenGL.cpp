#include "VertexArrayOpenGL.h"
#include <glad/glad.h>

//implement function for vertexarray
VertexArrayOpenGL::VertexArrayOpenGL()
{
	glGenVertexArrays(1, &m_RendererID);
}

VertexArrayOpenGL::~VertexArrayOpenGL()
{
	glDeleteVertexArrays(1, &m_RendererID);
}


void VertexArrayOpenGL::Bind() const
{
	glBindVertexArray(m_RendererID);
}

void VertexArrayOpenGL::Unbind() const
{
	glBindVertexArray(0);
}

void VertexArrayOpenGL::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
{
	glBindVertexArray(m_RendererID);
	vertexBuffer->Bind();
	const auto& layout = vertexBuffer->GetLayout();
	for (const auto& element : layout)
	{
		glEnableVertexAttribArray(element.GetComponentCount());
		glVertexAttribPointer(element.GetComponentCount(),
			element.GetComponentCount(),
			GL_FLOAT,
			element.Normalized ? GL_TRUE : GL_FALSE,
			layout.GetStride(),
			(const void*)element.Offset);
	}
	m_VertexBuffers.push_back(vertexBuffer);
}

void VertexArrayOpenGL::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
{
	glBindVertexArray(m_RendererID);
	indexBuffer->Bind();
	m_IndexBuffer = indexBuffer;
}


