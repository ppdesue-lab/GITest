#include "RenderCommand.h"

Scope<Renderer> RenderCommand::s_RendererAPI = Renderer::Create();

//renderer data for flush
Ref<RendererData> RenderCommand::s_RendererData = nullptr;


void RenderCommand::FlushLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& color)
{
	if (s_RendererData->LineCount >= s_RendererData->MaxLineVertices)
	{
		return;// Flush();
	}
	s_RendererData->LineVertexBufferBase[s_RendererData->LineCount].Position = p0;
	s_RendererData->LineVertexBufferBase[s_RendererData->LineCount].Color = color;
	s_RendererData->LineCount++;
	s_RendererData->LineVertexBufferBase[s_RendererData->LineCount].Position = p1;
	s_RendererData->LineVertexBufferBase[s_RendererData->LineCount].Color = color;
	s_RendererData->LineCount++;
}

void RenderCommand::Flush()
{
	if (s_RendererData->LineCount == 0)
		return;

	uint32_t dataSize = s_RendererData->LineCount * sizeof(LineVertex);
	s_RendererData->LineVertexBuffer->SetData(s_RendererData->LineVertexBufferBase, dataSize);
	s_RendererData->LineShader->Bind();
	DrawLines(s_RendererData->LineVertexArray, s_RendererData->LineCount);
	s_RendererData->DrawLineCalls++;
	s_RendererData->LineCount = 0;
}


