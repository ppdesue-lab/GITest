#include "stdsfx.h"
#include "DX11Shader.h"
#include "DX11Context.h"

#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

const DX11Shader* DX11Shader::s_CurrentShader = nullptr;

DX11Shader::DX11Shader(const std::string& filepath)
	: m_Name(filepath)
{
	Compile(ReadFile(filepath));
	CreateConstantBuffer();
}

DX11Shader::DX11Shader(const std::string& name, const std::string& source, ShaderType type)
	: DX11Shader(name, type == ShaderType::Vertex ? source : std::string(), type == ShaderType::Fragment ? source : std::string())
{
}

DX11Shader::DX11Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
	: m_Name(name)
{
	(void)vertexSrc;
	(void)fragmentSrc;
	Compile(BuiltinSource(name));
	CreateConstantBuffer();
}

void DX11Shader::Bind() const
{
	auto context = DX11Context::GetDeviceContext();
	context->VSSetShader(m_VertexShader.Get(), nullptr, 0);
	context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
	ID3D11Buffer* cbuffer = m_ConstantBuffer.Get();
	context->VSSetConstantBuffers(0, 1, &cbuffer);
	context->PSSetConstantBuffers(0, 1, &cbuffer);
	s_CurrentShader = this;
	UploadConstants();
}

void DX11Shader::Unbind() const
{
	DX11Context::GetDeviceContext()->VSSetShader(nullptr, nullptr, 0);
	DX11Context::GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
}

void DX11Shader::SetInt(const std::string& name, int value)
{
	if (name == "u_matcapTex")
		m_Constants.ints[0] = value;
	UploadConstants();
}

void DX11Shader::SetIntArray(const std::string& name, int* values, uint32_t count)
{
	(void)name;
	memcpy(m_Constants.ints, values, std::min<uint32_t>(count, 16) * sizeof(int));
	UploadConstants();
}

void DX11Shader::SetVec3Array(const std::string& name, float* values, uint32_t count)
{
	if (name == "shCoeffs")
	{
		for (uint32_t i = 0; i < std::min<uint32_t>(count, 9); ++i)
			m_Constants.shCoeffs[i] = glm::vec4(values[i * 3 + 0], values[i * 3 + 1], values[i * 3 + 2], 0.0f);
	}
	UploadConstants();
}

void DX11Shader::SetFloat(const std::string& name, float value)
{
	(void)name;
	m_Constants.values[0].x = value;
	UploadConstants();
}

void DX11Shader::SetFloat2(const std::string& name, const glm::vec2& value)
{
	(void)name;
	m_Constants.values[0] = glm::vec4(value, 0.0f, 0.0f);
	UploadConstants();
}

void DX11Shader::SetFloat3(const std::string& name, const glm::vec3& value)
{
	if (name == "u_lightPos") m_Constants.u_lightPos = glm::vec4(value, 0.0f);
	else if (name == "u_viewPos") m_Constants.u_viewPos = glm::vec4(value, 0.0f);
	else if (name == "u_lightColor") m_Constants.u_lightColor = glm::vec4(value, 0.0f);
	else if (name == "u_objectColor") m_Constants.u_objectColor = glm::vec4(value, 0.0f);
	else m_Constants.values[0] = glm::vec4(value, 0.0f);
	UploadConstants();
}

void DX11Shader::SetFloat4(const std::string& name, const glm::vec4& value)
{
	(void)name;
	m_Constants.values[0] = value;
	UploadConstants();
}

void DX11Shader::SetMat4(const std::string& name, const glm::mat4& value)
{
	if (name == "u_View") m_Constants.u_View = value;
	else if (name == "u_Projection") m_Constants.u_Projection = value;
	else if (name == "u_Model") m_Constants.u_Model = value;
	else if (name == "u_invViewProj") m_Constants.u_invViewProj = value;
	UploadConstants();
}

void DX11Shader::ApplyInputLayout(const BufferLayout& layout) const
{
	std::string key;
	for (const auto& element : layout)
		key += element.Name + std::to_string((int)element.Type) + ";";

	auto it = m_InputLayouts.find(key);
	if (it == m_InputLayouts.end())
	{
		std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
		uint32_t slot = 0;
		for (const auto& element : layout)
			elements.push_back(ToInputElement(element, slot++));

		Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
		HRESULT hr = DX11Context::GetDevice()->CreateInputLayout(
			elements.data(),
			(UINT)elements.size(),
			m_VertexBlob->GetBufferPointer(),
			m_VertexBlob->GetBufferSize(),
			&inputLayout);
		if (FAILED(hr))
		{
			ERROR("Failed to create DX11 input layout for shader {}", m_Name);
			return;
		}
		it = m_InputLayouts.emplace(key, inputLayout).first;
	}

	DX11Context::GetDeviceContext()->IASetInputLayout(it->second.Get());
}

void DX11Shader::ApplyCurrentInputLayout(const BufferLayout& layout)
{
	if (s_CurrentShader)
		s_CurrentShader->ApplyInputLayout(layout);
}

void DX11Shader::Compile(const std::string& hlsl)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
#endif
	Microsoft::WRL::ComPtr<ID3DBlob> errors;
	Microsoft::WRL::ComPtr<ID3DBlob> pixelBlob;

	HRESULT hr = D3DCompile(hlsl.c_str(), hlsl.size(), m_Name.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &m_VertexBlob, &errors);
	if (FAILED(hr))
	{
		if (errors) ERROR("DX11 vertex shader compile error: {}", (const char*)errors->GetBufferPointer());
		throw std::runtime_error("DX11 vertex shader compilation failed");
	}
	hr = D3DCompile(hlsl.c_str(), hlsl.size(), m_Name.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, &pixelBlob, &errors);
	if (FAILED(hr))
	{
		if (errors) ERROR("DX11 pixel shader compile error: {}", (const char*)errors->GetBufferPointer());
		throw std::runtime_error("DX11 pixel shader compilation failed");
	}

	DX11Context::GetDevice()->CreateVertexShader(m_VertexBlob->GetBufferPointer(), m_VertexBlob->GetBufferSize(), nullptr, &m_VertexShader);
	DX11Context::GetDevice()->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &m_PixelShader);
}

void DX11Shader::CreateConstantBuffer()
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = (sizeof(Constants) + 15) / 16 * 16;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	DX11Context::GetDevice()->CreateBuffer(&desc, nullptr, &m_ConstantBuffer);
}

void DX11Shader::UploadConstants() const
{
	if (!m_ConstantBuffer)
		return;
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	auto context = DX11Context::GetDeviceContext();
	if (SUCCEEDED(context->Map(m_ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, &m_Constants, sizeof(Constants));
		context->Unmap(m_ConstantBuffer.Get(), 0);
	}
}

std::string DX11Shader::ReadFile(const std::string& filepath)
{
	std::ifstream in(filepath, std::ios::in | std::ios::binary);
	if (!in)
		return BuiltinSource("DefaultColor");
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string DX11Shader::BuiltinSource(const std::string& name) const
{
	if (name == "DefaultBackgroundSH")
	{
		return R"(
cbuffer KEngineConstants : register(b0)
{
	float4x4 u_View;
	float4x4 u_Projection;
	float4x4 u_Model;
	float4x4 u_invViewProj;
	float4 u_lightPos;
	float4 u_viewPos;
	float4 u_lightColor;
	float4 u_objectColor;
	float4 shCoeffs[9];
	float4 values[8];
	int4 ints[4];
};
struct VSIn { float3 Position : POSITION; };
struct VSOut { float4 Position : SV_POSITION; float3 Dir : TEXCOORD0; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float4 worldPos = mul(u_invViewProj, float4(input.Position, 1.0));
	worldPos /= worldPos.w;
	output.Dir = normalize(worldPos.xyz);
	output.Position = float4(input.Position, 1.0);
	return output;
}
float3 EvalSH(float3 dir)
{
	float x = dir.x, y = dir.y, z = dir.z;
	float3 result = 0.0;
	result += shCoeffs[0].rgb * 0.282095;
	result += shCoeffs[1].rgb * (0.488603 * y);
	result += shCoeffs[2].rgb * (0.488603 * z);
	result += shCoeffs[3].rgb * (0.488603 * x);
	result += shCoeffs[4].rgb * (1.092548 * x * y);
	result += shCoeffs[5].rgb * (1.092548 * y * z);
	result += shCoeffs[6].rgb * (0.315392 * (3.0 * z * z - 1.0));
	result += shCoeffs[7].rgb * (1.092548 * x * z);
	result += abs(shCoeffs[8].rgb * (0.546274 * (x * x - y * y)));
	return result;
}
float4 PSMain(VSOut input) : SV_TARGET { return float4(EvalSH(normalize(input.Dir)), 1.0); }
)";
	}

	if (name == "DefaultPhong")
	{
		return R"(
cbuffer KEngineConstants : register(b0)
{
	float4x4 u_View;
	float4x4 u_Projection;
	float4x4 u_Model;
	float4x4 u_invViewProj;
	float4 u_lightPos;
	float4 u_viewPos;
	float4 u_lightColor;
	float4 u_objectColor;
	float4 shCoeffs[9];
	float4 values[8];
	int4 ints[4];
};
struct VSIn { float3 Position : POSITION; float3 Normal : NORMAL; };
struct VSOut { float4 Position : SV_POSITION; float3 WorldPos : TEXCOORD0; float3 Normal : NORMAL; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float4 world = mul(u_Model, float4(input.Position, 1.0));
	output.WorldPos = world.xyz;
	output.Normal = mul((float3x3)u_Model, input.Normal);
	output.Position = mul(u_Projection, mul(u_View, world));
	return output;
}
float4 PSMain(VSOut input) : SV_TARGET
{
	float3 n = normalize(input.Normal);
	float3 lightDir = normalize(u_lightPos.xyz - input.WorldPos);
	float diff = max(dot(n, lightDir), 0.0);
	float3 viewDir = normalize(u_viewPos.xyz - input.WorldPos);
	float3 reflectDir = reflect(-lightDir, n);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
	float3 color = (0.1 * u_lightColor.rgb + diff * u_lightColor.rgb + 0.5 * spec * u_lightColor.rgb) * u_objectColor.rgb;
	return float4(color, 1.0);
}
)";
	}

	if (name == "DefaultMatcap")
	{
		return R"(
Texture2D u_matcapTex : register(t0);
SamplerState u_sampler : register(s0);
cbuffer KEngineConstants : register(b0)
{
	float4x4 u_View;
	float4x4 u_Projection;
	float4x4 u_Model;
	float4x4 u_invViewProj;
	float4 u_lightPos;
	float4 u_viewPos;
	float4 u_lightColor;
	float4 u_objectColor;
	float4 shCoeffs[9];
	float4 values[8];
	int4 ints[4];
};
struct VSIn { float3 Position : POSITION; float3 Normal : NORMAL; };
struct VSOut { float4 Position : SV_POSITION; float3 Normal : NORMAL; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	output.Normal = normalize(mul((float3x3)mul(u_View, u_Model), input.Normal));
	output.Position = mul(u_Projection, mul(u_View, mul(u_Model, float4(input.Position, 1.0))));
	return output;
}
float4 PSMain(VSOut input) : SV_TARGET
{
	float2 uv = normalize(input.Normal).xy * 0.5 + 0.5;
	return float4(u_matcapTex.Sample(u_sampler, uv).rgb, 1.0);
}
)";
	}

	return R"(
cbuffer KEngineConstants : register(b0)
{
	float4x4 u_View;
	float4x4 u_Projection;
	float4x4 u_Model;
	float4x4 u_invViewProj;
	float4 u_lightPos;
	float4 u_viewPos;
	float4 u_lightColor;
	float4 u_objectColor;
	float4 shCoeffs[9];
	float4 values[8];
	int4 ints[4];
};
struct VSIn { float3 Position : POSITION; float4 Color : COLOR; };
struct VSOut { float4 Position : SV_POSITION; float4 Color : COLOR; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	output.Color = input.Color;
	output.Position = mul(u_Projection, mul(u_View, mul(u_Model, float4(input.Position, 1.0))));
	return output;
}
float4 PSMain(VSOut input) : SV_TARGET { return input.Color; }
)";
}

D3D11_INPUT_ELEMENT_DESC DX11Shader::ToInputElement(const BufferElement& element, uint32_t slot) const
{
	D3D11_INPUT_ELEMENT_DESC desc = {};
	if (element.Name.find("Position") != std::string::npos) desc.SemanticName = "POSITION";
	else if (element.Name.find("Color") != std::string::npos) desc.SemanticName = "COLOR";
	else if (element.Name.find("Normal") != std::string::npos) desc.SemanticName = "NORMAL";
	else desc.SemanticName = "TEXCOORD";
	desc.SemanticIndex = 0;
	desc.InputSlot = 0;
	desc.AlignedByteOffset = (UINT)element.Offset;
	desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	desc.InstanceDataStepRate = 0;

	switch (element.Type)
	{
	case ShaderDataType::Float: desc.Format = DXGI_FORMAT_R32_FLOAT; break;
	case ShaderDataType::Float2: desc.Format = DXGI_FORMAT_R32G32_FLOAT; break;
	case ShaderDataType::Float3: desc.Format = DXGI_FORMAT_R32G32B32_FLOAT; break;
	case ShaderDataType::Float4: desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
	case ShaderDataType::Int: desc.Format = DXGI_FORMAT_R32_SINT; break;
	case ShaderDataType::Int2: desc.Format = DXGI_FORMAT_R32G32_SINT; break;
	case ShaderDataType::Int3: desc.Format = DXGI_FORMAT_R32G32B32_SINT; break;
	case ShaderDataType::Int4: desc.Format = DXGI_FORMAT_R32G32B32A32_SINT; break;
	default: desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
	}
	(void)slot;
	return desc;
}
