#include "OpenGLRenderer.h"

#include <glad/glad.h>

namespace
{
class OpenGLLineInstanceBuffer : public RendererLineInstanceBuffer
{
public:
	OpenGLLineInstanceBuffer(const RendererLineInstance* lines, uint32_t lineCount)
		: m_LineCount(lineCount)
	{
		if (!lines || lineCount == 0)
		{
			m_LineCount = 0;
			return;
		}

		glCreateBuffers(1, &m_Buffer);
		glNamedBufferData(m_Buffer, (GLsizeiptr)(lineCount * sizeof(RendererLineInstance)),
			lines, GL_STATIC_DRAW);
	}

	~OpenGLLineInstanceBuffer() override
	{
		if (m_Buffer != 0)
		{
			glDeleteBuffers(1, &m_Buffer);
			m_Buffer = 0;
		}
	}

	uint32_t GetLineCount() const override { return m_LineCount; }
	uint32_t GetBuffer() const { return m_Buffer; }
	bool IsValid() const { return m_Buffer != 0 && m_LineCount > 0; }

private:
	uint32_t m_Buffer = 0;
	uint32_t m_LineCount = 0;
};

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
		m_LineInstanceCapacity = 0;
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

bool OpenGLRenderer::EnsureInstancedLineShader()
{
	if (!m_InstancedLineShader)
	{
		const std::string vertexSource = R"(
            #version 430 core
            struct LineInstance
            {
                vec4 Start;
                vec4 End;
                vec4 Color;
                vec4 Meta0;
                vec4 Meta1;
            };

            layout(std430, binding = 0) readonly buffer LineInstances
            {
                LineInstance u_Lines[];
            };

            uniform mat4 u_View;
            uniform mat4 u_Projection;
            uniform mat4 u_Model;
            uniform vec2 u_ViewportSize;
            uniform float u_LineWidth;

            out vec4 v_Color;
            out vec4 v_Meta0;
            out float v_LinearDepth;

            vec4 ResolveLineColor(vec4 inputColor, vec4 meta0)
            {
                if (meta0.x >= 0.5 && meta0.x < 1.5)
                {
                    float colorType = meta0.y;
                    if (colorType < 0.5)
                        return vec4(1.0, 0.12, 0.08, 1.0);
                    if (colorType < 1.5)
                        return vec4(0.08, 0.88, 0.18, 1.0);
                    return vec4(0.15, 0.45, 1.0, 1.0);
                }
                return inputColor;
            }

            void main()
            {
                LineInstance line = u_Lines[gl_InstanceID];
                vec4 localStart = line.Start;
                vec4 localEnd = line.End;
                vec4 color = line.Color;
                vec4 meta0 = line.Meta0;

                vec4 viewStart = u_View * u_Model * vec4(localStart.xyz, 1.0);
                vec4 viewEnd = u_View * u_Model * vec4(localEnd.xyz, 1.0);
                vec4 clipStart = u_Projection * viewStart;
                vec4 clipEnd = u_Projection * viewEnd;
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
                v_Color = ResolveLineColor(color, meta0);
                v_Meta0 = meta0;
                v_LinearDepth = max(-(useEnd ? viewEnd.z : viewStart.z), 0.0001);
            }
        )";

		const std::string fragmentSource = R"(
            #version 430 core
            layout(location = 0) out vec4 o_Color;
            in vec4 v_Color;
            in vec4 v_Meta0;
            in float v_LinearDepth;
            void main()
            {
                vec4 color = v_Color;
                if (v_Meta0.x >= 0.5 && v_Meta0.x < 1.5)
                {
                    float depthMetric = log2(v_LinearDepth + 10.0);
                    float depthSlope = abs(dFdx(depthMetric)) + abs(dFdy(depthMetric));
                    float shade = exp(-60.0 * depthSlope * 4.0);
                    shade = clamp(mix(1.0, shade, 0.6), 0.45, 1.0);
                    color.rgb *= shade;
                }
                o_Color = color;
            }
		)";
		m_InstancedLineShader = Shader::Create("Object2DInstancedLine", vertexSource, fragmentSource);
	}

	return m_InstancedLineShader != nullptr;
}

Ref<RendererLineInstanceBuffer> OpenGLRenderer::CreateLineInstanceBuffer(
	const RendererLineInstance* lines, uint32_t lineCount)
{
	if (!lines || lineCount == 0)
		return nullptr;
	Ref<OpenGLLineInstanceBuffer> buffer = CreateRef<OpenGLLineInstanceBuffer>(lines, lineCount);
	return buffer->IsValid() ? buffer : nullptr;
}

bool OpenGLRenderer::DrawInstancedLines(const RendererLineInstance* lines, uint32_t lineCount,
	const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
	const glm::vec2& viewportSize)
{
	if (!lines || lineCount == 0)
		return false;

	if (!EnsureInstancedLineShader())
		return false;

	if (m_LineInstanceSSBO == 0)
		glCreateBuffers(1, &m_LineInstanceSSBO);
	if (m_InstancedLineVAO == 0)
		glCreateVertexArrays(1, &m_InstancedLineVAO);

	const uint32_t instanceBytes = lineCount * (uint32_t)sizeof(RendererLineInstance);
	if (lineCount > m_LineInstanceCapacity)
	{
		m_LineInstanceCapacity = lineCount;
		glNamedBufferData(m_LineInstanceSSBO, (GLsizeiptr)instanceBytes, lines, GL_DYNAMIC_DRAW);
	}
	else
	{
		glNamedBufferSubData(m_LineInstanceSSBO, 0, (GLsizeiptr)instanceBytes, lines);
	}

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

bool OpenGLRenderer::DrawInstancedLines(const Ref<RendererLineInstanceBuffer>& lineBuffer, uint32_t lineCount,
	const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
	const glm::vec2& viewportSize)
{
	if (!lineBuffer || lineCount == 0)
		return false;

	if (!EnsureInstancedLineShader())
		return false;

	const OpenGLLineInstanceBuffer* buffer = dynamic_cast<const OpenGLLineInstanceBuffer*>(lineBuffer.get());
	if (!buffer || !buffer->IsValid())
		return false;

	if (m_InstancedLineVAO == 0)
		glCreateVertexArrays(1, &m_InstancedLineVAO);

	const uint32_t drawCount = glm::min(lineCount, buffer->GetLineCount());
	const glm::vec2 safeViewportSize(glm::max(viewportSize.x, 1.0f), glm::max(viewportSize.y, 1.0f));
	m_InstancedLineShader->Bind();
	m_InstancedLineShader->SetMat4("u_View", view);
	m_InstancedLineShader->SetMat4("u_Projection", proj);
	m_InstancedLineShader->SetMat4("u_Model", model);
	m_InstancedLineShader->SetFloat2("u_ViewportSize", safeViewportSize);
	m_InstancedLineShader->SetFloat("u_LineWidth", m_LineWidth);

	glBindVertexArray(m_InstancedLineVAO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer->GetBuffer());
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)drawCount);
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
