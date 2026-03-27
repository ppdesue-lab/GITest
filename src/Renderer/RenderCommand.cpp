#include "RenderCommand.h"
Scope<Renderer> RenderCommand::s_RendererAPI = Renderer::Create();

//renderer data for flush
Ref<RendererData> RenderCommand::s_RendererData = nullptr;


RendererData::RendererData()
{
    LineVertexArray = VertexArray::Create();
    LineVertexBuffer = VertexBuffer::Create(MaxLineVertices * sizeof(glm::vec3));
    LineVertexBuffer->SetLayout({ {
        BufferElement{ShaderDataType::Float3, "a_Position"},
        BufferElement{ShaderDataType::Float4, "a_Color"}
        } });

    LineVertexArray->AddVertexBuffer(LineVertexBuffer);

    LineVertexBufferBase = new LineVertex[MaxLineVertices];

	//---
	QuadVertexArray = VertexArray::Create();
	QuadVertexBuffer = VertexBuffer::Create(MaxQuadVertices * sizeof(glm::vec3));
	QuadVertexBuffer->SetLayout({ {
		BufferElement{ShaderDataType::Float3, "a_Position"},
		BufferElement{ShaderDataType::Float4, "a_Color"}
		} });

	QuadVertexArray->AddVertexBuffer(QuadVertexBuffer);

	QuadVertexBufferBase = new LineVertex[MaxQuadVertices];


}

void RenderCommand::FlushLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
{
	if (s_RendererData->LineCount+1 >= s_RendererData->MaxLineVertices)
	{
		return;// Flush();
	}
	LineVertex* startVertex = s_RendererData->LineVertexBufferBase + s_RendererData->LineCount;

	startVertex[0].Position = p0;
	startVertex[0].Color = color;
	startVertex[1].Position = p1;
	startVertex[1].Color = color;
	s_RendererData->LineCount+=2;
}

void RenderCommand::FlushQuad(const glm::vec3& p0, const glm::vec3& p1,
	const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color)
{
	if (s_RendererData->QuadCount + 6 >= s_RendererData->MaxQuadVertices)
	{
		return;// Flush();
	}
	LineVertex* startVertex = s_RendererData->QuadVertexBufferBase + s_RendererData->QuadCount;

	startVertex[0].Position = p0;
	startVertex[0].Color = color;
	startVertex[1].Position = p2;
	startVertex[1].Color = color;
	startVertex[2].Position = p1;
	startVertex[2].Color = color;
	s_RendererData->QuadCount += 3;

	startVertex[3].Position = p0;
	startVertex[3].Color = color;
	startVertex[4].Position = p2;
	startVertex[4].Color = color;
	startVertex[5].Position = p3;
	startVertex[5].Color = color;
	s_RendererData->QuadCount += 3;
}

void RenderCommand::FlushTriangle(const glm::vec3& p0, const glm::vec3& p1,
	const glm::vec3& p2,  const glm::vec4& color)
{
	if (s_RendererData->QuadCount + 3 >= s_RendererData->MaxQuadVertices)
	{
		return;// Flush();
	}
	LineVertex* startVertex = s_RendererData->QuadVertexBufferBase + s_RendererData->QuadCount;

	startVertex[0].Position = p0;
	startVertex[0].Color = color;
	startVertex[1].Position = p2;
	startVertex[1].Color = color;
	startVertex[2].Position = p1;
	startVertex[2].Color = color;
	s_RendererData->QuadCount += 3;
}


void RenderCommand::Flush()
{
	if (s_RendererData->LineCount)
	{
		SetLineWidth(3.0f);
		uint32_t dataSize = s_RendererData->LineCount * sizeof(LineVertex);
		s_RendererData->LineVertexBuffer->SetData(s_RendererData->LineVertexBufferBase, dataSize);
		if (!s_RendererData->LineShader) s_RendererData->LineShader = ShaderLibrary::Instance()->Get("DefaultColor");
		s_RendererData->LineShader->Bind();
		DrawLines(s_RendererData->LineVertexArray, s_RendererData->LineCount);
		s_RendererData->DrawLineCalls++;
		s_RendererData->LineCount = 0;
	}

	//draw quad
	if (s_RendererData->QuadCount)
	{
		uint32_t dataSize = s_RendererData->QuadCount * sizeof(LineVertex);
		s_RendererData->QuadVertexBuffer->SetData(s_RendererData->QuadVertexBufferBase, dataSize);
		if (!s_RendererData->QuadShader) s_RendererData->QuadShader = ShaderLibrary::Instance()->Get("DefaultColor");
		s_RendererData->QuadShader->Bind();
		DrawIndexed(s_RendererData->QuadVertexArray, s_RendererData->QuadCount);
		s_RendererData->QuadCount = 0;
	}
}


