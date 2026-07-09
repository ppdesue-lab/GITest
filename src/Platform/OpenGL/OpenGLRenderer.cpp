#include "OpenGLRenderer.h"

#include <glad/glad.h>

namespace
{
GLenum ResolveCapability(const std::string& capability)
{
	if (capability == "BLEND") return GL_BLEND;
	if (capability == "CULL_FACE") return GL_CULL_FACE;
	if (capability == "DEPTH_TEST") return GL_DEPTH_TEST;
	if (capability == "LINE_SMOOTH") return GL_LINE_SMOOTH;
	if (capability == "POLYGON_OFFSET_FILL") return GL_POLYGON_OFFSET_FILL;
	return 0;
}
}

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

void OpenGLRenderer::DrawPoints(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
{
	vertexArray->Bind();
	if (!vertexArray->GetIndexBuffer())
		glDrawArrays(GL_POINTS, 0, vertexCount);
	else
		glDrawElements(GL_POINTS, vertexCount, GL_UNSIGNED_INT, nullptr);
}

void OpenGLRenderer::SetLineWidth(float width)
{
	glLineWidth(width);
}

void OpenGLRenderer::SetPointSize(float size)
{
	glPointSize(size);
}

void OpenGLRenderer::Enable(const std::string& capability)
{
	const GLenum value = ResolveCapability(capability);
	if (value != 0)
		glEnable(value);
}

void OpenGLRenderer::Disable(const std::string& capability)
{
	const GLenum value = ResolveCapability(capability);
	if (value != 0)
		glDisable(value);
}

void OpenGLRenderer::Cull(const std::string& face)
{
	if (face == "Front")
		glCullFace(GL_FRONT);
	else if (face == "Back")
		glCullFace(GL_BACK);
	else if (face == "FrontAndBack")
		glCullFace(GL_FRONT_AND_BACK);
}

void OpenGLRenderer::EnableDepthTest(bool enable)
{
	if (enable)
		Enable("DEPTH_TEST");
	else
		Disable("DEPTH_TEST");
}

void OpenGLRenderer::SetDepthRange(float min, float max)
{
	glDepthRange(min, max);
}
