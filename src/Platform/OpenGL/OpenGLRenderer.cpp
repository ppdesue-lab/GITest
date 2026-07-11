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

OpenGLRenderer::~OpenGLRenderer()
{
	if (m_LineInstanceSSBO != 0)
	{
		glDeleteBuffers(1, &m_LineInstanceSSBO);
		m_LineInstanceSSBO = 0;
	}
	if (m_InstancedLineVAO != 0)
	{
		glDeleteVertexArrays(1, &m_InstancedLineVAO);
		m_InstancedLineVAO = 0;
	}
}

void OpenGLRenderer::Init()
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(m_LineWidth);
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

bool OpenGLRenderer::DrawInstancedLines(const RendererLineInstance* lines, uint32_t lineCount,
	const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
	const glm::vec2& viewportSize)
{
	if (!lines || lineCount == 0)
		return false;

	if (!m_InstancedLineShader)
	{
		const std::string vertexSource = R"(
            #version 430 core
            layout(std430, binding = 0) readonly buffer LineInstances
            {
                vec4 u_Lines[];
            };

            uniform mat4 u_View;
            uniform mat4 u_Projection;
            uniform mat4 u_Model;
            uniform vec2 u_ViewportSize;
            uniform float u_LineWidth;

            out vec4 v_Color;

            void main()
            {
                int lineBase = gl_InstanceID * 3;
                vec4 localStart = u_Lines[lineBase + 0];
                vec4 localEnd = u_Lines[lineBase + 1];
                vec4 color = u_Lines[lineBase + 2];

                vec4 clipStart = u_Projection * u_View * u_Model * vec4(localStart.xyz, 1.0);
                vec4 clipEnd = u_Projection * u_View * u_Model * vec4(localEnd.xyz, 1.0);
                vec2 startNdc = clipStart.xy / max(abs(clipStart.w), 0.000001);
                vec2 endNdc = clipEnd.xy / max(abs(clipEnd.w), 0.000001);
                vec2 startScreen = startNdc * u_ViewportSize;
                vec2 endScreen = endNdc * u_ViewportSize;
                vec2 dir = endScreen - startScreen;
                float len = length(dir);
                vec2 normal = len > 0.0001 ? vec2(-dir.y, dir.x) / len : vec2(0.0, 1.0);

                float side = (gl_VertexID == 0 || gl_VertexID == 2) ? -1.0 : 1.0;
                bool useEnd = gl_VertexID >= 2;
                vec4 clipPos = useEnd ? clipEnd : clipStart;
                vec2 viewport = max(u_ViewportSize, vec2(1.0));
                vec2 offsetNdc = normal * side * (u_LineWidth / viewport);

                gl_Position = clipPos + vec4(offsetNdc * clipPos.w, 0.0, 0.0);
                v_Color = color;
            }
        )";

		const std::string fragmentSource = R"(
            #version 430 core
            layout(location = 0) out vec4 o_Color;
            in vec4 v_Color;
            void main()
            {
                o_Color = v_Color;
            }
        )";
		m_InstancedLineShader = Shader::Create("Object2DInstancedLine", vertexSource, fragmentSource);
	}

	if (!m_InstancedLineShader)
		return false;

	if (m_LineInstanceSSBO == 0)
		glCreateBuffers(1, &m_LineInstanceSSBO);
	if (m_InstancedLineVAO == 0)
		glCreateVertexArrays(1, &m_InstancedLineVAO);

	glNamedBufferData(m_LineInstanceSSBO, (GLsizeiptr)(lineCount * sizeof(RendererLineInstance)), lines, GL_DYNAMIC_DRAW);

	const glm::vec2 safeViewportSize(glm::max(viewportSize.x, 1.0f), glm::max(viewportSize.y, 1.0f));
	m_InstancedLineShader->Bind();
	m_InstancedLineShader->SetMat4("u_View", view);
	m_InstancedLineShader->SetMat4("u_Projection", proj);
	m_InstancedLineShader->SetMat4("u_Model", model);
	m_InstancedLineShader->SetFloat2("u_ViewportSize", safeViewportSize);
	m_InstancedLineShader->SetFloat("u_LineWidth", m_LineWidth);

	glBindVertexArray(m_InstancedLineVAO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_LineInstanceSSBO);
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)lineCount);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	glBindVertexArray(0);
	return true;
}

void OpenGLRenderer::SetLineWidth(float width)
{
	m_LineWidth = glm::max(width, 1.0f);
	glLineWidth(m_LineWidth);
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
