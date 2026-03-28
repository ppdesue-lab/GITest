#include "OpenGLRenderer.h"

#include <glad/glad.h>


void OpenGLRenderer::Init()
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
}

void OpenGLRenderer::SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	glViewport(x, y, w, h);
}

void OpenGLRenderer::SetClearColor(const glm::vec4& color)
{
	glClearColor(color.r, color.g, color.b, color.a);
}

void OpenGLRenderer::Clear()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
	vertexArray->Bind();
	uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
	if (!vertexArray->GetIndexBuffer())
		glDrawArrays(GL_TRIANGLES, 0, count);
	else
		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

void OpenGLRenderer::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
{
	vertexArray->Bind();
	if (!vertexArray->GetIndexBuffer())
		glDrawArrays(GL_LINES, 0, indexCount);
	else
		glDrawElements(GL_LINES, indexCount,GL_UNSIGNED_INT,nullptr);
}

void OpenGLRenderer::SetLineWidth(float width)
{
	glLineWidth(width);
}

void OpenGLRenderer::EnableDepthTest(bool enable)
{
	if (enable)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
}

void OpenGLRenderer::SetDepthRange(float min, float max)
{
	glDepthRange(min, max);
}