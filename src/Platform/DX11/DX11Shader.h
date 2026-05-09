#pragma once

#include <Renderer/Shader.h>
#include <Renderer/Buffer.h>
#include <d3d11.h>
#include <wrl/client.h>

class DX11Shader : public Shader
{
public:
	DX11Shader(const std::string& filepath);
	DX11Shader(const std::string& name, const std::string& source, ShaderType type);
	DX11Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
	~DX11Shader() override = default;

	void Bind() const override;
	void Unbind() const override;
	void SetInt(const std::string& name, int value) override;
	void SetIntArray(const std::string& name, int* values, uint32_t count) override;
	void SetVec3Array(const std::string& name, float* values, uint32_t count) override;
	void SetFloat(const std::string& name, float value) override;
	void SetFloat2(const std::string& name, const glm::vec2& value) override;
	void SetFloat3(const std::string& name, const glm::vec3& value) override;
	void SetFloat4(const std::string& name, const glm::vec4& value) override;
	void SetMat4(const std::string& name, const glm::mat4& value) override;
	const std::string& GetName() const override { return m_Name; }

	void ApplyInputLayout(const BufferLayout& layout) const;
	static void ApplyCurrentInputLayout(const BufferLayout& layout);

private:
	struct Constants
	{
		glm::mat4 u_View{ 1.0f };
		glm::mat4 u_Projection{ 1.0f };
		glm::mat4 u_Model{ 1.0f };
		glm::mat4 u_invViewProj{ 1.0f };
		glm::vec4 u_lightPos{ 0.0f };
		glm::vec4 u_viewPos{ 0.0f };
		glm::vec4 u_lightColor{ 1.0f };
		glm::vec4 u_objectColor{ 1.0f };
		glm::vec4 shCoeffs[9]{};
		glm::vec4 values[8]{};
		int ints[16]{};
	};

	void Compile(const std::string& hlsl);
	void CreateConstantBuffer();
	void UploadConstants() const;
	std::string ReadFile(const std::string& filepath);
	std::string BuiltinSource(const std::string& name) const;
	D3D11_INPUT_ELEMENT_DESC ToInputElement(const BufferElement& element, uint32_t slot) const;

private:
	std::string m_Name;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;
	Microsoft::WRL::ComPtr<ID3DBlob> m_VertexBlob;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_ConstantBuffer;
	mutable std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11InputLayout>> m_InputLayouts;
	mutable Constants m_Constants;

	static const DX11Shader* s_CurrentShader;
};
