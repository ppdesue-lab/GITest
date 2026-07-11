#include "stdsfx.h"
#include "DX11Shader.h"
#include "DX11Context.h"
#include "Renderer/ProfileTimer.h"

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
	context->GSSetShader(nullptr, nullptr, 0);
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
	DX11Context::GetDeviceContext()->GSSetShader(nullptr, nullptr, 0);
}

void DX11Shader::SetInt(const std::string& name, int value)
{
    if (name == "u_ObjectID") m_Constants.ints[0] = value;
    else if (name == "u_XZInput") m_Constants.ints[1] = value;
    else if (name == "u_HasAlbedoMap") m_Constants.ints[2] = value;
    else if (name == "u_HasNormalMap") m_Constants.ints[3] = value;
    else if (name == "u_HasMetallicMap") m_Constants.ints[4] = value;
    else if (name == "u_HasRoughnessMap") m_Constants.ints[5] = value;
    else if (name == "u_HasAOMap") m_Constants.ints[6] = value;
    else if (name == "u_MetallicMapChannel") m_Constants.ints[7] = value;
    else if (name == "u_RoughnessMapChannel") m_Constants.ints[8] = value;
    else if (name == "u_AOMapChannel") m_Constants.ints[9] = value;
    else if (name == "u_pbrDebugMode") m_Constants.ints[10] = value;
    else if (name == "u_BackgroundMode") m_Constants.ints[11] = value;
    else if (name == "u_FillSelectedPixels") m_Constants.ints[12] = value;
    else if (name == "u_TransparentPass") m_Constants.ints[13] = value;
    else if (name == "u_iblEnabled") m_Constants.ints[14] = value;
    else if (name == "u_cascadeCount") { m_Constants.u_shadowParams.w = (float)value; }
    else if (name == "u_debugCascadeView") { m_Constants.ints[15] = value; }
    else if (name == "u_giEnabled") { m_Constants.u_giParams.x = (float)value; }
    else if (name == "u_giDebugMode") { m_Constants.u_giParams.y = (float)value; }
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
	else if (name == "u_probeIrradiance")
	{
		for (uint32_t i = 0; i < std::min<uint32_t>(count, 64); ++i)
			m_Constants.u_probeIrradiance[i] = glm::vec4(values[i * 3 + 0], values[i * 3 + 1], values[i * 3 + 2], 0.0f);
	}
	UploadConstants();
}

void DX11Shader::SetFloat(const std::string& name, float value)
{
    if (name == "u_Roughness") m_Constants.u_AlbedoRoughness.w = value;
    else if (name == "u_Metallic") m_Constants.u_MetallicAOOpacityEdge.x = value;
    else if (name == "u_AO") m_Constants.u_MetallicAOOpacityEdge.y = value;
    else if (name == "u_ObjectOpacity") m_Constants.u_MetallicAOOpacityEdge.z = value;
    else if (name == "u_EdgeWidth") m_Constants.u_MetallicAOOpacityEdge.w = value;
    else if (name == "u_XZInputY") m_Constants.values[0].x = value;
    else if (name == "u_shadowMapSize") m_Constants.u_shadowParams.x = value;
    else if (name == "u_shadowConstantBias") m_Constants.u_shadowParams.y = value;
    else if (name == "u_shadowSlopeBias") m_Constants.u_shadowParams.z = value;
    else if (name == "u_giIntensity") m_Constants.u_giParams.z = value;
    else if (name == "u_giSpacing") m_Constants.u_giParams.w = value;
    else if (name.rfind("u_cascadeDistances[", 0) == 0)
    {
        const size_t open = name.find('[');
        const size_t close = name.find(']', open == std::string::npos ? 0 : open);
        int idx = -1;
        if (open != std::string::npos && close != std::string::npos && close > open + 1)
            idx = std::atoi(name.substr(open + 1, close - open - 1).c_str());
        if (idx >= 0 && idx < 4)
            ((float*)&m_Constants.u_cascadeDistances)[idx] = value;
    }
    else if (name.rfind("u_cascadeTexelSize[", 0) == 0)
    {
        const size_t open = name.find('[');
        const size_t close = name.find(']', open == std::string::npos ? 0 : open);
        int idx = -1;
        if (open != std::string::npos && close != std::string::npos && close > open + 1)
            idx = std::atoi(name.substr(open + 1, close - open - 1).c_str());
        if (idx >= 0 && idx < 4)
            ((float*)&m_Constants.u_cascadeTexelSize)[idx] = value;
    }
    else if (name == "u_Radius" || name == "u_Bias" || name == "u_Strength" ||
             name == "u_NegInvR2" || name == "u_MaxRadiusPixels" || name == "u_TanAngleBias" ||
             name == "u_PowerExponent" || name == "u_BlurSharpness" || name == "u_EdgeThreshold" ||
             name == "u_SubpixelQuality" || name == "u_SpanMax")
    {
        m_Constants.values[0].x = value;
    }
    else
    {
        m_Constants.values[0].x = value;
    }
    UploadConstants();
}

void DX11Shader::SetFloat2(const std::string& name, const glm::vec2& value)
{
	if (name == "u_ViewportSize" || name == "u_ScreenSize")
		m_Constants.u_ViewportSize = glm::vec4(value, 0.0f, 0.0f);
	else
		m_Constants.values[0] = glm::vec4(value, 0.0f, 0.0f);
	UploadConstants();
}

void DX11Shader::SetFloat3(const std::string& name, const glm::vec3& value)
{
	if (name == "u_lightPos") m_Constants.u_lightPos = glm::vec4(value, 0.0f);
	else if (name == "u_viewPos") m_Constants.u_viewPos = glm::vec4(value, 0.0f);
	else if (name == "u_lightColor") m_Constants.u_lightColor = glm::vec4(value, 0.0f);
	else if (name == "u_objectColor") m_Constants.u_objectColor = glm::vec4(value, 0.0f);
	else if (name == "u_lightDir") m_Constants.u_lightDir = glm::vec4(value, 0.0f);
	else if (name == "u_Albedo") m_Constants.u_AlbedoRoughness = glm::vec4(value, m_Constants.u_AlbedoRoughness.w);
	else if (name == "u_giOrigin") m_Constants.u_giOrigin = glm::vec4(value, 0.0f);
	else if (name == "u_giCounts") m_Constants.u_giCounts = glm::vec4(value, 0.0f);
	else m_Constants.values[0] = glm::vec4(value, 0.0f);
	UploadConstants();
}

void DX11Shader::SetFloat4(const std::string& name, const glm::vec4& value)
{
	if (name == "u_EdgeColor")
		m_Constants.u_EdgeColor = value;
	else
		m_Constants.values[0] = value;
	UploadConstants();
}

void DX11Shader::SetMat4(const std::string& name, const glm::mat4& value)
{
    if (name == "u_View") m_Constants.u_View = value;
    else if (name == "u_Projection") m_Constants.u_Projection = value;
    else if (name == "u_Model") m_Constants.u_Model = value;
    else if (name == "u_invViewProj") m_Constants.u_invViewProj = value;
    else if (name == "u_LightViewProj")
    {
        m_Constants.u_lightViewProj[0] = value;
    }
    else if (name.rfind("u_lightViewProj[", 0) == 0)
    {
        int idx = name[16] - '0';
        if (idx >= 0 && idx < 4)
            m_Constants.u_lightViewProj[idx] = value;
    }
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

void DX11Shader::SetPointExpansion(bool enabled, float pointSizePixels)
{
	auto context = DX11Context::GetDeviceContext();
	if (!context)
		return;

	if (!enabled || !s_CurrentShader || !s_CurrentShader->m_PointGeometryShader || !s_CurrentShader->m_PointConstantBuffer)
	{
		context->GSSetShader(nullptr, nullptr, 0);
		return;
	}

	UINT viewportCount = 1;
	D3D11_VIEWPORT viewport = {};
	context->RSGetViewports(&viewportCount, &viewport);
	if (viewport.Width <= 0.0f || viewport.Height <= 0.0f)
	{
		context->GSSetShader(nullptr, nullptr, 0);
		return;
	}

	const glm::vec4 params(glm::max(pointSizePixels, 1.0f), viewport.Width, viewport.Height, 0.0f);
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (SUCCEEDED(context->Map(s_CurrentShader->m_PointConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, &params, sizeof(params));
		context->Unmap(s_CurrentShader->m_PointConstantBuffer.Get(), 0);
	}

	ID3D11Buffer* pointCBuffer = s_CurrentShader->m_PointConstantBuffer.Get();
	context->GSSetConstantBuffers(1, 1, &pointCBuffer);
	context->GSSetShader(s_CurrentShader->m_PointGeometryShader.Get(), nullptr, 0);
}

static HRESULT SafeD3DCompileShader(const char* src, SIZE_T size, const char* name, const char* entry, const char* target, UINT flags, ID3DBlob** blob, ID3DBlob** errors)
{
	__try {
		return D3DCompile(src, size, name, nullptr, nullptr, entry, target, flags, 0, blob, errors);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return E_FAIL;
	}
}

void DX11Shader::Compile(const std::string& hlsl)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
#endif

	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	ID3DBlob* errors = nullptr;

	HRESULT hr = SafeD3DCompileShader(hlsl.c_str(), hlsl.size(), m_Name.c_str(), "VSMain", "vs_5_0", flags, &vsBlob, &errors);
	if (FAILED(hr))
	{
		if (errors) ERROR("DX11 vertex shader compile error ({}): {}", m_Name, (const char*)errors->GetBufferPointer());
		else ERROR("DX11 vertex shader compilation failed ({})", m_Name);
		if (errors) errors->Release();
		if (vsBlob) vsBlob->Release();
		throw std::runtime_error("DX11 vertex shader compilation failed");
	}
	m_VertexBlob.Attach(vsBlob);

	hr = SafeD3DCompileShader(hlsl.c_str(), hlsl.size(), m_Name.c_str(), "PSMain", "ps_5_0", flags, &psBlob, &errors);
	if (FAILED(hr))
	{
		if (errors) ERROR("DX11 pixel shader compile error ({}): {}", m_Name, (const char*)errors->GetBufferPointer());
		else ERROR("DX11 pixel shader compilation failed ({})", m_Name);
		if (errors) errors->Release();
		if (psBlob) psBlob->Release();
		throw std::runtime_error("DX11 pixel shader compilation failed");
	}
	Microsoft::WRL::ComPtr<ID3DBlob> pixelBlob(psBlob);

	DX11Context::GetDevice()->CreateVertexShader(m_VertexBlob->GetBufferPointer(), m_VertexBlob->GetBufferSize(), nullptr, &m_VertexShader);
	DX11Context::GetDevice()->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &m_PixelShader);
	CreatePointExpansionResources();
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

void DX11Shader::CreatePointExpansionResources()
{
	static const char* pointGS = R"(
struct VSOut { float4 Position : SV_POSITION; float4 Color : COLOR; };
cbuffer PointExpansionConstants : register(b1)
{
	float4 u_PointParams; // x=size pixels, y=viewport width, z=viewport height
};
[maxvertexcount(4)]
void GSMain(point VSOut input[1], inout TriangleStream<VSOut> stream)
{
	float4 center = input[0].Position;
	float2 halfSize = float2(u_PointParams.x / max(u_PointParams.y, 1.0), u_PointParams.x / max(u_PointParams.z, 1.0)) * center.w;
	float2 offsets[4] = {
		float2(-halfSize.x, -halfSize.y),
		float2(-halfSize.x,  halfSize.y),
		float2( halfSize.x, -halfSize.y),
		float2( halfSize.x,  halfSize.y)
	};
	for (int i = 0; i < 4; ++i)
	{
		VSOut output;
		output.Position = center + float4(offsets[i], 0.0, 0.0);
		output.Color = input[0].Color;
		stream.Append(output);
	}
}
)";

	ID3DBlob* gsBlob = nullptr;
	ID3DBlob* errors = nullptr;
	HRESULT hr = SafeD3DCompileShader(pointGS, strlen(pointGS), (m_Name + ".PointGS").c_str(), "GSMain", "gs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS, &gsBlob, &errors);
	if (FAILED(hr))
	{
		if (errors)
			errors->Release();
		if (gsBlob)
			gsBlob->Release();
		return;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> geometryBlob(gsBlob);
	if (FAILED(DX11Context::GetDevice()->CreateGeometryShader(
		geometryBlob->GetBufferPointer(), geometryBlob->GetBufferSize(), nullptr, &m_PointGeometryShader)))
		return;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(glm::vec4);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	DX11Context::GetDevice()->CreateBuffer(&desc, nullptr, &m_PointConstantBuffer);
}

void DX11Shader::UploadConstants() const
{
    PROFILE_SCOPE("DX11.UploadConstants");
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
    const char* commonHeader = R"(
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
	float4 u_lightDir;
	float4 u_AlbedoRoughness;
	float4 u_MetallicAOOpacityEdge;
	float4 u_EdgeColor;
	float4 u_ViewportSize;
	float4 shCoeffs[9];
	float4x4 u_lightViewProj[4];
	float4 u_cascadeDistances;
	float4 u_cascadeTexelSize;
	float4 u_shadowParams; // x=shadowMapSize, y=constBias, z=slopeBias, w=cascadeCount
	float4 u_giParams; // x=enabled, y=debug, z=intensity, w=spacing
	float4 u_giOrigin;
	float4 u_giCounts;
	float4 u_probeIrradiance[64];
	float4 values[8];
	int4 ints[4];
};
)";

	if (name == "ObjectPickup")
	{
		return std::string(commonHeader) + R"(
struct VSIn { float3 Position : POSITION; };
struct VSOut { float4 Position : SV_POSITION; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 position = ints[0].y != 0 ? float3(input.Position.x, values[0].x, input.Position.y) : input.Position;
	output.Position = mul(u_Projection, mul(u_View, mul(u_Model, float4(position, 1.0))));
	return output;
}
int PSMain(VSOut input) : SV_TARGET
{
	return ints[0].x;
}
)";
	}

	if (name == "SelectedMask")
	{
		return std::string(commonHeader) + R"(
struct VSIn { float3 Position : POSITION; };
struct VSOut { float4 Position : SV_POSITION; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float3 position = ints[0].y != 0 ? float3(input.Position.x, values[0].x, input.Position.y) : input.Position;
	output.Position = mul(u_Projection, mul(u_View, mul(u_Model, float4(position, 1.0))));
	return output;
}
int PSMain(VSOut input) : SV_TARGET
{
	return 1;
}
)";
	}

	if (name == "SelectedEdgeComposite")
	{
		return std::string(commonHeader) + R"(
Texture2D u_SceneColor : register(t0);
Texture2D<int> u_SelectedMask : register(t1);
SamplerState u_sampler : register(s0);
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
VSOut VSMain(uint vertexID : SV_VertexID)
{
	float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
	VSOut output;
	float2 position = positions[vertexID];
	output.Position = float4(position, 0.0, 1.0);
	output.UV = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
	return output;
}
int SampleMask(int2 pixel)
{
	int width;
	int height;
	u_SelectedMask.GetDimensions(width, height);
	pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
	return u_SelectedMask.Load(int3(pixel, 0));
}
float4 PSMain(VSOut input) : SV_TARGET
{
	int2 pixel = int2(input.Position.xy);
	bool centerSelected = SampleMask(pixel) != 0;
	bool neighborSelected = false;
	int radius = clamp((int)ceil(u_MetallicAOOpacityEdge.w), 1, 8);
	for (int y = -radius; y <= radius; ++y)
	{
		for (int x = -radius; x <= radius; ++x)
		{
			if (x == 0 && y == 0) continue;
			if (length(float2(x, y)) > (float)radius + 0.001) continue;
			neighborSelected = neighborSelected || SampleMask(pixel + int2(x, y)) != 0;
		}
	}

	float4 scene = u_SceneColor.Sample(u_sampler, input.UV);
	if (ints[3].x != 0)
	{
		bool hasEmptyNeighbor = false;
		for (int fy = -1; fy <= 1; ++fy)
			for (int fx = -1; fx <= 1; ++fx)
				if (fx != 0 || fy != 0)
					hasEmptyNeighbor = hasEmptyNeighbor || SampleMask(pixel + int2(fx, fy)) == 0;
		return centerSelected && hasEmptyNeighbor ? u_EdgeColor : scene;
	}
	return (!centerSelected && neighborSelected) ? u_EdgeColor : scene;
}
)";
	}

	if (name == "DefaultBackgroundSH")
	{
		return R"(
Texture2D u_EnvironmentEquirect : register(t0);
SamplerState u_envSampler : register(s0);
)" + std::string(commonHeader) + R"(
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
float2 SampleSphericalMap(float3 d)
{
	const float2 invAtan = float2(0.15915494, 0.31830989);
	float2 uv = float2(atan2(d.z, d.x), asin(d.y));
	return uv * invAtan + 0.5;
}
float4 PSMain(VSOut input) : SV_TARGET
{
	float3 dir = normalize(input.Dir);
	float3 color = EvalSH(dir);
	if (ints[2].w == 1)
	{
		color = u_EnvironmentEquirect.Sample(u_envSampler, SampleSphericalMap(dir)).rgb;
		color = color / (color + 1.0);
		color = pow(saturate(color), 1.0 / 2.2);
	}
	return float4(color, 1.0);
}
)";
	}

	if (name == "ShadowDepth")
	{
		return std::string(commonHeader) + R"(
struct VSIn { float3 Position : POSITION; };
struct VSOut { float4 Position : SV_POSITION; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	output.Position = mul(u_lightViewProj[0], mul(u_Model, float4(input.Position, 1.0)));
	return output;
}
void PSMain(VSOut input) { }
)";
	}

	if (name == "DefaultPhong")
	{
		return std::string(commonHeader) + R"(
Texture2DArray u_shadowMap : register(t2);
SamplerState u_shadowSampler : register(s2);
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
int SelectCascade(float viewDepth)
{
	int cascadeCount = (int)u_shadowParams.w;
	viewDepth = max(viewDepth, 0.0);
	for (int i = 0; i < cascadeCount - 1; i++)
	{
		if (viewDepth <= u_cascadeDistances[i + 1]) return i;
	}
	return max(cascadeCount - 1, 0);
}
float SampleShadowCascade(int cascade, float3 worldPos, float3 normal);
float ShadowFactor(float3 worldPos, float3 normal)
{
	int cascadeCount = (int)u_shadowParams.w;
	if (cascadeCount <= 0) return 1.0;
	float viewDepth = max(-dot(float4(worldPos, 1.0), mul(u_View, float4(0,0,1,0)).xyz + u_View._m03_m13_m23_m33 * 0), 0.0);
	float4 viewPos = mul(u_View, float4(worldPos, 1.0));
	viewDepth = max(-viewPos.z, 0.0);
	int cascade = SelectCascade(viewDepth);
	float shadow = SampleShadowCascade(cascade, worldPos, normal);
	if (cascade < cascadeCount - 1)
	{
		float splitEnd = u_cascadeDistances[cascade + 1];
		float blendRange = max((splitEnd - u_cascadeDistances[cascade]) * 0.12, 1.0);
		float blend = saturate((viewDepth - (splitEnd - blendRange)) / blendRange);
		shadow = lerp(shadow, SampleShadowCascade(cascade + 1, worldPos, normal), blend);
	}
	return shadow;
}
float SampleShadowCascade(int cascade, float3 worldPos, float3 normal)
{
	float4 lightClip = mul(u_lightViewProj[cascade], float4(worldPos, 1.0));
	float3 ndc = lightClip.xyz / lightClip.w;
	if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
	float3 uvz = float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
	float biasScale = clamp(sqrt(max(u_cascadeTexelSize[cascade], 0.00001) / max(u_cascadeTexelSize.x, 0.00001)), 0.5, 4.0);
	float bias = max(u_shadowParams.z * (1.0 - dot(normalize(normal), normalize(-u_lightDir.xyz))), u_shadowParams.y) * biasScale;
	float visibility = 0.0;
	float2 texel = 1.0 / u_shadowParams.xx;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
		{
			float depth = u_shadowMap.Sample(u_shadowSampler, float3(uvz.xy + float2(x, y) * texel, cascade));
			visibility += uvz.z - bias > depth ? 0.3 : 1.0;
		}
	return visibility / 9.0;
}
int ProbeIndex(int3 c, int3 counts)
{
	return c.x + counts.x * (c.y + counts.y * c.z);
}
float3 SampleProbeGI(float3 worldPos, float3 normal)
{
	if (u_giParams.x == 0.0) return float3(0.0, 0.0, 0.0);
	int3 counts = max(int3(u_giCounts.xyz + 0.5), int3(1, 1, 1));
	float3 maxCoord = float3(counts - int3(1, 1, 1));
	float3 grid = clamp((worldPos - u_giOrigin.xyz) / max(u_giParams.w, 0.001), float3(0.0, 0.0, 0.0), maxCoord);
	int3 c0 = int3(floor(grid));
	int3 c1 = min(c0 + int3(1, 1, 1), counts - int3(1, 1, 1));
	float3 f = frac(grid);
	float3 c000 = u_probeIrradiance[ProbeIndex(int3(c0.x, c0.y, c0.z), counts)].rgb;
	float3 c100 = u_probeIrradiance[ProbeIndex(int3(c1.x, c0.y, c0.z), counts)].rgb;
	float3 c010 = u_probeIrradiance[ProbeIndex(int3(c0.x, c1.y, c0.z), counts)].rgb;
	float3 c110 = u_probeIrradiance[ProbeIndex(int3(c1.x, c1.y, c0.z), counts)].rgb;
	float3 c001 = u_probeIrradiance[ProbeIndex(int3(c0.x, c0.y, c1.z), counts)].rgb;
	float3 c101 = u_probeIrradiance[ProbeIndex(int3(c1.x, c0.y, c1.z), counts)].rgb;
	float3 c011 = u_probeIrradiance[ProbeIndex(int3(c0.x, c1.y, c1.z), counts)].rgb;
	float3 c111 = u_probeIrradiance[ProbeIndex(int3(c1.x, c1.y, c1.z), counts)].rgb;
	float3 low = lerp(lerp(c000, c100, f.x), lerp(c010, c110, f.x), f.y);
	float3 high = lerp(lerp(c001, c101, f.x), lerp(c011, c111, f.x), f.y);
	float diffuseResponse = 0.3 + 0.7 * max(normalize(normal).y, 0.0);
	return lerp(low, high, f.z) * diffuseResponse * u_giParams.z;
}
struct PSOut { float4 Color : SV_TARGET0; float4 GPosition : SV_TARGET1; float4 GNormal : SV_TARGET2; };
PSOut PSMain(VSOut input)
{
	PSOut output;
	float3 n = normalize(input.Normal);
	float3 lightDir = normalize(-u_lightDir.xyz);
	float diff = max(dot(n, lightDir), 0.0);
	float3 viewDir = normalize(u_viewPos.xyz - input.WorldPos);
	float3 reflectDir = reflect(-lightDir, n);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
	float3 color;
	if (ints[3].w != 0)
	{
		int cascade = SelectCascade(max(-(mul(u_View, float4(input.WorldPos, 1.0)).z), 0.0));
		float3 cascadeColor = cascade == 0 ? float3(1.0, 0.2, 0.2) : (cascade == 1 ? float3(0.2, 1.0, 0.2) : (cascade == 2 ? float3(0.2, 0.4, 1.0) : float3(1.0, 1.0, 0.2)));
		color = cascadeColor * 0.85 + 0.05;
	}
	else
	{
		float shadow = 1.0;
		if ((int)u_shadowParams.w > 0)
			shadow = ShadowFactor(input.WorldPos, n);
		float3 indirect = SampleProbeGI(input.WorldPos, n) * u_objectColor.rgb;
		if (u_giParams.y == 1.0)
			color = indirect;
		else
			color = (0.1 * u_lightColor.rgb + (diff * u_lightColor.rgb + 0.5 * spec * u_lightColor.rgb) * shadow) * u_objectColor.rgb + indirect;
	}
	output.Color = float4(color, 1.0);
	output.GPosition = float4(input.WorldPos, 1.0);
	output.GNormal = float4(n, 1.0);
	return output;
}
)";
	}

	if (name == "DefaultMatcap")
	{
		return R"(
Texture2D u_matcapTex : register(t0);
SamplerState u_sampler : register(s0);
Texture2DArray u_shadowMap : register(t2);
SamplerState u_shadowSampler : register(s2);
)" + std::string(commonHeader) + R"(
struct VSIn { float3 Position : POSITION; float3 Normal : NORMAL; };
struct VSOut { float4 Position : SV_POSITION; float3 WorldPos : TEXCOORD0; float3 Normal : NORMAL; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float4 world = mul(u_Model, float4(input.Position, 1.0));
	output.WorldPos = world.xyz;
	output.Normal = normalize(mul((float3x3)mul(u_View, u_Model), input.Normal));
	output.Position = mul(u_Projection, mul(u_View, world));
	return output;
}
int SelectCascade(float viewDepth)
{
	int cascadeCount = (int)u_shadowParams.w;
	viewDepth = max(viewDepth, 0.0);
	for (int i = 0; i < cascadeCount - 1; i++)
	{
		if (viewDepth <= u_cascadeDistances[i + 1]) return i;
	}
	return max(cascadeCount - 1, 0);
}
float SampleShadowCascade(int cascade, float3 worldPos);
float ShadowFactor(float3 worldPos)
{
	int cascadeCount = (int)u_shadowParams.w;
	if (cascadeCount <= 0) return 1.0;
	float4 viewPos = mul(u_View, float4(worldPos, 1.0));
	float viewDepth = max(-viewPos.z, 0.0);
	int cascade = SelectCascade(viewDepth);
	float shadow = SampleShadowCascade(cascade, worldPos);
	if (cascade < cascadeCount - 1)
	{
		float splitEnd = u_cascadeDistances[cascade + 1];
		float blendRange = max((splitEnd - u_cascadeDistances[cascade]) * 0.12, 1.0);
		float blend = saturate((viewDepth - (splitEnd - blendRange)) / blendRange);
		shadow = lerp(shadow, SampleShadowCascade(cascade + 1, worldPos), blend);
	}
	return shadow;
}
float SampleShadowCascade(int cascade, float3 worldPos)
{
	float4 lightClip = mul(u_lightViewProj[cascade], float4(worldPos, 1.0));
	float3 ndc = lightClip.xyz / lightClip.w;
	if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
	float3 uvz = float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
	float biasScale = clamp(sqrt(max(u_cascadeTexelSize[cascade], 0.00001) / max(u_cascadeTexelSize.x, 0.00001)), 0.5, 4.0);
	float bias = u_shadowParams.y * biasScale;
	float visibility = 0.0;
	float2 texel = 1.0 / u_shadowParams.xx;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
		{
			float depth = u_shadowMap.Sample(u_shadowSampler, float3(uvz.xy + float2(x, y) * texel, cascade));
			visibility += uvz.z - bias > depth ? 0.3 : 1.0;
		}
	return visibility / 9.0;
}
struct PSOut { float4 Color : SV_TARGET0; float4 GPosition : SV_TARGET1; float4 GNormal : SV_TARGET2; };
PSOut PSMain(VSOut input)
{
	PSOut output;
	float2 uv = normalize(input.Normal).xy * 0.5 + 0.5;
	float3 matColor = u_matcapTex.Sample(u_sampler, uv).rgb;
	float shadow = 1.0;
	if ((int)u_shadowParams.w > 0)
		shadow = ShadowFactor(input.WorldPos);
	output.Color = float4(matColor * lerp(0.3, 1.0, shadow), 1.0);
	output.GPosition = float4(input.WorldPos, 1.0);
	output.GNormal = float4(normalize(input.Normal), 1.0);
	return output;
}
)";
	}

	if (name == "DefaultPBR")
	{
		return std::string(commonHeader) + R"(
Texture2DArray u_shadowMap : register(t2);
SamplerState u_shadowSampler : register(s2);
Texture2D u_AlbedoMap : register(t6);
Texture2D u_NormalMap : register(t7);
Texture2D u_MetallicMap : register(t8);
Texture2D u_RoughnessMap : register(t9);
Texture2D u_AOMap : register(t10);
Texture2D u_EnvironmentEquirect : register(t11);
SamplerState u_sampler : register(s6);
SamplerState u_envSampler : register(s11);
struct VSIn { float3 Position : POSITION; float3 Normal : NORMAL; float2 TexCoord : TEXCOORD; };
struct VSOut { float4 Position : SV_POSITION; float3 WorldPos : TEXCOORD0; float3 Normal : NORMAL; float2 TexCoord : TEXCOORD1; };
VSOut VSMain(VSIn input)
{
	VSOut output;
	float4 world = mul(u_Model, float4(input.Position, 1.0));
	output.WorldPos = world.xyz;
	output.Normal = mul((float3x3)u_Model, input.Normal);
	output.TexCoord = input.TexCoord;
	output.Position = mul(u_Projection, mul(u_View, world));
	return output;
}
struct PSOut { float4 Color : SV_TARGET0; float4 GPosition : SV_TARGET1; float4 GNormal : SV_TARGET2; };
float SampleChannel(Texture2D tex, float2 uv, int channel)
{
	float4 value = tex.Sample(u_sampler, uv);
	if (channel == 1) return value.g;
	if (channel == 2) return value.b;
	if (channel == 3) return value.a;
	return value.r;
}
float3 GetNormal(VSOut input)
{
	float3 n = normalize(input.Normal);
	if (ints[0].w == 0)
		return n;
	float3 tangentNormal = u_NormalMap.Sample(u_sampler, input.TexCoord).xyz * 2.0 - 1.0;
	float3 dp1 = ddx(input.WorldPos);
	float3 dp2 = ddy(input.WorldPos);
	float2 duv1 = ddx(input.TexCoord);
	float2 duv2 = ddy(input.TexCoord);
	float3 t = normalize(dp1 * duv2.y - dp2 * duv1.y);
	float3 b = normalize(-cross(n, t));
	return normalize(mul(tangentNormal, float3x3(t, b, n)));
}
float DistributionGGX(float3 n, float3 h, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float ndoth = max(dot(n, h), 0.0);
	float denominator = ndoth * ndoth * (a2 - 1.0) + 1.0;
	return a2 / max(3.14159265 * denominator * denominator, 0.0001);
}
float GeometrySchlickGGX(float ndotv, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
}
float3 FresnelSchlick(float cosTheta, float3 f0)
{
	return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}
float2 SampleSphericalMap(float3 d)
{
	const float2 invAtan = float2(0.15915494, 0.31830989);
	float2 uv = float2(atan2(d.z, d.x), asin(d.y));
	return uv * invAtan + 0.5;
}
int SelectCascade(float viewDepth)
{
	int cascadeCount = (int)u_shadowParams.w;
	viewDepth = max(viewDepth, 0.0);
	for (int i = 0; i < cascadeCount - 1; i++)
	{
		if (viewDepth <= u_cascadeDistances[i + 1]) return i;
	}
	return max(cascadeCount - 1, 0);
}
float SampleShadowCascade(int cascade, float3 worldPos, float3 normal);
float ShadowFactor(float3 worldPos, float3 normal)
{
	int cascadeCount = (int)u_shadowParams.w;
	if (cascadeCount <= 0) return 1.0;
	float4 viewPos = mul(u_View, float4(worldPos, 1.0));
	float viewDepth = max(-viewPos.z, 0.0);
	int cascade = SelectCascade(viewDepth);
	float shadow = SampleShadowCascade(cascade, worldPos, normal);
	if (cascade < cascadeCount - 1)
	{
		float splitEnd = u_cascadeDistances[cascade + 1];
		float blendRange = max((splitEnd - u_cascadeDistances[cascade]) * 0.12, 1.0);
		float blend = saturate((viewDepth - (splitEnd - blendRange)) / blendRange);
		shadow = lerp(shadow, SampleShadowCascade(cascade + 1, worldPos, normal), blend);
	}
	return shadow;
}
float SampleShadowCascade(int cascade, float3 worldPos, float3 normal)
{
	float4 lightClip = mul(u_lightViewProj[cascade], float4(worldPos, 1.0));
	float3 ndc = lightClip.xyz / lightClip.w;
	if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
	float3 uvz = float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
	float biasScale = clamp(sqrt(max(u_cascadeTexelSize[cascade], 0.00001) / max(u_cascadeTexelSize.x, 0.00001)), 0.5, 4.0);
	float bias = max(u_shadowParams.z * (1.0 - dot(normalize(normal), normalize(-u_lightDir.xyz))), u_shadowParams.y) * biasScale;
	float visibility = 0.0;
	float2 texel = 1.0 / u_shadowParams.xx;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
		{
			float depth = u_shadowMap.Sample(u_shadowSampler, float3(uvz.xy + float2(x, y) * texel, cascade));
			visibility += uvz.z - bias > depth ? 0.3 : 1.0;
		}
	return visibility / 9.0;
}
int ProbeIndex(int3 c, int3 counts)
{
	return c.x + counts.x * (c.y + counts.y * c.z);
}
float3 SampleProbeGI(float3 worldPos, float3 normal, float3 albedo, float metallic)
{
	if (u_giParams.x == 0.0) return float3(0.0, 0.0, 0.0);
	int3 counts = max(int3(u_giCounts.xyz + 0.5), int3(1, 1, 1));
	float3 maxCoord = float3(counts - int3(1, 1, 1));
	float3 grid = clamp((worldPos - u_giOrigin.xyz) / max(u_giParams.w, 0.001), float3(0.0, 0.0, 0.0), maxCoord);
	int3 c0 = int3(floor(grid));
	int3 c1 = min(c0 + int3(1, 1, 1), counts - int3(1, 1, 1));
	float3 f = frac(grid);
	float3 c000 = u_probeIrradiance[ProbeIndex(int3(c0.x, c0.y, c0.z), counts)].rgb;
	float3 c100 = u_probeIrradiance[ProbeIndex(int3(c1.x, c0.y, c0.z), counts)].rgb;
	float3 c010 = u_probeIrradiance[ProbeIndex(int3(c0.x, c1.y, c0.z), counts)].rgb;
	float3 c110 = u_probeIrradiance[ProbeIndex(int3(c1.x, c1.y, c0.z), counts)].rgb;
	float3 c001 = u_probeIrradiance[ProbeIndex(int3(c0.x, c0.y, c1.z), counts)].rgb;
	float3 c101 = u_probeIrradiance[ProbeIndex(int3(c1.x, c0.y, c1.z), counts)].rgb;
	float3 c011 = u_probeIrradiance[ProbeIndex(int3(c0.x, c1.y, c1.z), counts)].rgb;
	float3 c111 = u_probeIrradiance[ProbeIndex(int3(c1.x, c1.y, c1.z), counts)].rgb;
	float3 low = lerp(lerp(c000, c100, f.x), lerp(c010, c110, f.x), f.y);
	float3 high = lerp(lerp(c001, c101, f.x), lerp(c011, c111, f.x), f.y);
	float diffuseResponse = 0.3 + 0.7 * max(normalize(normal).y, 0.0);
	float3 irradiance = lerp(low, high, f.z) * diffuseResponse * u_giParams.z;
	return irradiance * albedo * (1.0 - metallic);
}
PSOut PSMain(VSOut input)
{
	PSOut output;
	float3 albedo = ints[0].z != 0 ? pow(abs(u_AlbedoMap.Sample(u_sampler, input.TexCoord).rgb), 2.2) * u_AlbedoRoughness.rgb : u_AlbedoRoughness.rgb;
	float roughness = saturate(ints[1].y != 0 ? SampleChannel(u_RoughnessMap, input.TexCoord, ints[2].x) : u_AlbedoRoughness.w);
	roughness = max(roughness, 0.04);
	float metallic = saturate(ints[1].x != 0 ? SampleChannel(u_MetallicMap, input.TexCoord, ints[1].w) : u_MetallicAOOpacityEdge.x);
	float ao = saturate(ints[1].z != 0 ? SampleChannel(u_AOMap, input.TexCoord, ints[2].y) : u_MetallicAOOpacityEdge.y);
	float alpha = u_MetallicAOOpacityEdge.z * (ints[0].z != 0 ? u_AlbedoMap.Sample(u_sampler, input.TexCoord).a : 1.0);
	clip(alpha - 0.001);
	float3 n = GetNormal(input);
	float3 v = normalize(u_viewPos.xyz - input.WorldPos);
	float3 l = normalize(-u_lightDir.xyz);
	float3 h = normalize(v + l);
	float ndotl = max(dot(n, l), 0.0);
	float ndotv = max(dot(n, v), 0.0);
	float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	float3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);
	float d = DistributionGGX(n, h, roughness);
	float g = GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
	float3 specular = d * g * f / max(4.0 * ndotv * ndotl, 0.001);
	float3 kd = (1.0 - f) * (1.0 - metallic);
	float shadow = 1.0;
	if ((int)u_shadowParams.w > 0)
		shadow = ShadowFactor(input.WorldPos, n);
	float3 direct = (kd * albedo / 3.14159265 + specular) * u_lightColor.rgb * ndotl * shadow;
	float3 envDiffuse = u_EnvironmentEquirect.Sample(u_envSampler, SampleSphericalMap(n)).rgb;
	float3 envSpecular = u_EnvironmentEquirect.SampleLevel(u_envSampler, SampleSphericalMap(reflect(-v, n)), 0.0).rgb;
	float3 ambient = albedo * lerp(float3(0.05, 0.06, 0.075), envDiffuse, ints[3].z != 0 ? 0.25 : 0.0) * ao;
	float3 environmentSpecular = envSpecular * f * (1.0 - roughness) * (ints[3].z != 0 ? 0.18 : 0.0);
	float3 indirect = SampleProbeGI(input.WorldPos, n, albedo, metallic);
	float3 color = ambient + indirect + direct;
	if (ints[3].w != 0) // debugCascadeView
	{
		int cascade = SelectCascade(max(-(mul(u_View, float4(input.WorldPos, 1.0)).z), 0.0));
		float3 cascadeColor = cascade == 0 ? float3(1.0, 0.2, 0.2) : (cascade == 1 ? float3(0.2, 1.0, 0.2) : (cascade == 2 ? float3(0.2, 0.4, 1.0) : float3(1.0, 1.0, 0.2)));
		color = cascadeColor * 0.85 + 0.05;
	}
	else if (ints[2].z == 1) color = pow(saturate(albedo), 1.0 / 2.2);
	else if (ints[2].z == 2) color = n * 0.5 + 0.5;
	else if (ints[2].z == 3) color = ambient / (ambient + 1.0);
	else if (ints[2].z == 4) color = environmentSpecular / (environmentSpecular + 1.0);
	else if (u_giParams.y == 1.0) color = indirect / (indirect + 1.0);
	else
	{
		color += environmentSpecular;
		color = color / (color + 1.0);
		color = pow(saturate(color), 1.0 / 2.2);
	}
	output.Color = float4(color, alpha);
	output.GPosition = float4(input.WorldPos, 1.0);
	output.GNormal = float4(n, 1.0);
	return output;
}
)";
	}

	return std::string(commonHeader) + R"(
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
