#pragma once

#include <Renderer/Renderer.h>

class DX11Renderer : public Renderer
{
public:
	void Init() override;
	void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
	void SetClearColor(const glm::vec4& color) override;
	void Clear() override;
	void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
	void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t indexCount) override;
	void DrawPoints(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
	void SetLineWidth(float width) override;
	void SetPointSize(float size) override;
	void Enable(const std::string& capability) override;
	void Disable(const std::string& capability) override;
	void Cull(const std::string& face) override;
	void EnableDepthTest(bool enable) override;
	void SetDepthRange(float min = 0.0f, float max = 1.0f) override;

private:
	void ApplyRasterizerState();

	glm::vec4 m_ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	bool m_CullEnabled = false;
	std::string m_CullFace = "Back";
};
