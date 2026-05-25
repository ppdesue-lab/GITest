#include "stdsfx.h"
#include "Shader.h"
#include "Renderer.h"

#include <Platform/OpenGL/OpenGLShader.h>
#ifdef G_DX11
#include <Platform/DX11/DX11Shader.h>
#endif


std::unordered_map<std::string, Ref<Shader>> ShaderLibrary::m_Shaders;
ShaderLibrary* ShaderLibrary::s_Instance;

Ref<Shader> Shader::Create(const std::string& filepath)
{
    switch(Renderer::GetAPI())
    {
        case Renderer::API::None:    ERROR("RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::OpenGL:  return CreateRef<OpenGLShader>(filepath);
#ifdef G_DX11
        case Renderer::API::DX11:    return CreateRef<DX11Shader>(filepath);
#endif
    }

    ERROR("Unknown RendererAPI!");
    return nullptr;
}

Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
{
    switch(Renderer::GetAPI())
    {
        case Renderer::API::None:    ERROR("RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::OpenGL:  return CreateRef<OpenGLShader>(name, vertexSrc, fragmentSrc);
#ifdef G_DX11
        case Renderer::API::DX11:    return CreateRef<DX11Shader>(name, vertexSrc, fragmentSrc);
#endif
    }

    ERROR("Unknown RendererAPI!");
    return nullptr;
}

Ref<Shader> Shader::Create(const std::string& name, const std::string& source, ShaderType type)
{
    switch(Renderer::GetAPI())
    {
        case Renderer::API::None:    ERROR("RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::OpenGL:  return CreateRef<OpenGLShader>(name, source, type);
#ifdef G_DX11
        case Renderer::API::DX11:    return CreateRef<DX11Shader>(name, source, type);
#endif
    }

    ERROR("Unknown RendererAPI!");
    return nullptr;
}

void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
{
	if(Exists(name))
        TRACE("Shader already exists!");
	m_Shaders[name] = shader;
}

void ShaderLibrary::Add(const Ref<Shader>& shader)
{
	auto& name = shader->GetName();
	Add(name, shader);
}

Ref<Shader> ShaderLibrary::Load(const std::string& filepath)
{
	auto shader = Shader::Create(filepath);
	Add(shader);
	return shader;
}

Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
{
	assert(name.size());
	auto shader = Shader::Create(filepath);
	Add(name, shader);
	return shader;
}

Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& vSource,const std::string& fSource)
{
	assert(name.size());

	auto shader = Shader::Create(name,vSource,fSource);
	Add(name, shader);
	return shader;
}

Ref<Shader> ShaderLibrary::Load(const std::string name,const std::string& source, ShaderType type)
{
	assert(name.size());
	std::string vSource, fSource;
	switch (type)
	{
	case ShaderType::Vertex:
		vSource = source;
		break;
	case ShaderType::Fragment:
		fSource = source;
		break;
	default:
		ERROR("Unsupported shader type!");
		return nullptr;
	}
	auto shader = Shader::Create(name, vSource, fSource);
	Add(name, shader);
	return shader;
}

Ref<Shader> ShaderLibrary::Get(const std::string& name)
{
	if(!Exists(name))TRACE("Shader [{}] not found!", name);
	return m_Shaders[name];
}

bool ShaderLibrary::Exists(const std::string& name)
{
	return m_Shaders.find(name) != m_Shaders.end();
}

void ShaderLibrary::LoadDefault()
{
	//opengl buildin shader
#pragma region OpenGL default shader
	{
		auto vSource = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			uniform mat4 u_Model;
			out vec4 v_Color;
			void main()
			{
				v_Color = a_Color;
				gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
			}
		)";
		auto fSource = R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;
			in vec4 v_Color;
			void main()
			{
				color = v_Color;
			}
		)";
		Load("DefaultColor", vSource, fSource);
	}
#pragma endregion

#pragma region phong shader with CSM shadow
	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Normal;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			uniform mat4 u_Model;
			out vec3 v_Position;
			out vec3 v_Normal;
			void main()
			{
				v_Normal = mat3(transpose(inverse(u_Model))) * a_Normal;
				vec4 fragPos = u_Model * vec4(a_Position, 1.0);
				v_Position = fragPos.xyz;
				gl_Position = u_Projection * u_View * fragPos;
			}
		)";

		auto fSource = R"(
			#version 330 core
			layout(location = 0) out vec4 color;
			layout(location = 1) out vec4 gPosition;
			layout(location = 2) out vec4 gNormal;
			in vec3 v_Position;
			in vec3 v_Normal;
			uniform mat4 u_View;
			uniform vec3 u_lightPos;
			uniform vec3 u_viewPos;
			uniform vec3 u_lightColor;
			uniform vec3 u_objectColor;
			uniform vec3 u_lightDir;
			uniform int u_cascadeCount;
			uniform float u_cascadeDistances[4];
			uniform mat4 u_lightViewProj[4];
			uniform sampler2DArray u_shadowMap;
			uniform float u_shadowMapSize;
			uniform int u_debugCascadeView;
			uniform int u_giEnabled;
			uniform int u_giDebugMode;
			uniform float u_giIntensity;
			uniform vec3 u_giOrigin;
			uniform float u_giSpacing;
			uniform vec3 u_giCounts;
			uniform vec3 u_probeIrradiance[64];

			vec3 CascadeColor(int cascade)
			{
				if (cascade == 0) return vec3(1.0, 0.2, 0.2);
				if (cascade == 1) return vec3(0.2, 1.0, 0.2);
				if (cascade == 2) return vec3(0.2, 0.4, 1.0);
				return vec3(1.0, 1.0, 0.2);
			}

			int SelectCascade(float viewDepth)
			{
				viewDepth = max(viewDepth, 0.0);
				for (int i = 0; i < u_cascadeCount - 1; i++)
					if (viewDepth <= u_cascadeDistances[i + 1]) return i;
				return u_cascadeCount - 1;
			}

			float ShadowVisibility(vec3 uvz, int cascade, float bias)
			{
				vec2 texelSize = vec2(1.0 / u_shadowMapSize);
				float visible = 0.0;
				for (int y = -1; y <= 1; y++)
				{
					for (int x = -1; x <= 1; x++)
					{
						float depth = texture(u_shadowMap, vec3(uvz.xy + vec2(x, y) * texelSize, cascade)).r;
						visible += (uvz.z - bias > depth) ? 0.3 : 1.0;
					}
				}
				return visible / 9.0;
			}

			float ShadowFactor(vec3 worldPos, vec3 normal)
			{
				vec4 viewPos = u_View * vec4(worldPos, 1.0);
				float viewDepth = max(-viewPos.z, 0.0);
				int cascade = SelectCascade(viewDepth);
				vec4 lightClip = u_lightViewProj[cascade] * vec4(worldPos, 1.0);
				vec3 lightNDC = lightClip.xyz / lightClip.w;
				if (lightNDC.x < -1.0 || lightNDC.x > 1.0 ||
				    lightNDC.y < -1.0 || lightNDC.y > 1.0 ||
				    lightNDC.z < -1.0 || lightNDC.z > 1.0) return 1.0;
				vec3 uvz = lightNDC * 0.5 + 0.5;
				float bias = max(0.005 * (1.0 - dot(normalize(normal), normalize(-u_lightDir))), 0.001);
				return ShadowVisibility(uvz, cascade, bias);
			}

			int ProbeIndex(ivec3 c, ivec3 counts)
			{
				return c.x + counts.x * (c.y + counts.y * c.z);
			}

			vec3 SampleProbeGI(vec3 worldPos, vec3 normal)
			{
				if (u_giEnabled == 0)
					return vec3(0.0);

				ivec3 counts = max(ivec3(u_giCounts + vec3(0.5)), ivec3(1));
				vec3 maxCoord = vec3(counts - ivec3(1));
				vec3 grid = clamp((worldPos - u_giOrigin) / max(u_giSpacing, 0.001), vec3(0.0), maxCoord);
				ivec3 c0 = ivec3(floor(grid));
				ivec3 c1 = min(c0 + ivec3(1), counts - ivec3(1));
				vec3 f = fract(grid);

				vec3 c000 = u_probeIrradiance[ProbeIndex(ivec3(c0.x, c0.y, c0.z), counts)];
				vec3 c100 = u_probeIrradiance[ProbeIndex(ivec3(c1.x, c0.y, c0.z), counts)];
				vec3 c010 = u_probeIrradiance[ProbeIndex(ivec3(c0.x, c1.y, c0.z), counts)];
				vec3 c110 = u_probeIrradiance[ProbeIndex(ivec3(c1.x, c1.y, c0.z), counts)];
				vec3 c001 = u_probeIrradiance[ProbeIndex(ivec3(c0.x, c0.y, c1.z), counts)];
				vec3 c101 = u_probeIrradiance[ProbeIndex(ivec3(c1.x, c0.y, c1.z), counts)];
				vec3 c011 = u_probeIrradiance[ProbeIndex(ivec3(c0.x, c1.y, c1.z), counts)];
				vec3 c111 = u_probeIrradiance[ProbeIndex(ivec3(c1.x, c1.y, c1.z), counts)];
				vec3 low = mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y);
				vec3 high = mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y);
				float diffuseResponse = 0.3 + 0.7 * max(normalize(normal).y, 0.0);
				return mix(low, high, f.z) * diffuseResponse * u_giIntensity;
			}

			void main()
			{
				vec3 norm = normalize(v_Normal);
				gPosition = vec4(v_Position, 1.0);
				gNormal = vec4(norm, 1.0);
				if (u_debugCascadeView != 0)
				{
					vec4 viewPos = u_View * vec4(v_Position, 1.0);
					int cascade = SelectCascade(max(-viewPos.z, 0.0));
					color = vec4(CascadeColor(cascade) * 0.85 + vec3(0.05), 1.0);
					return;
				}

				vec3 ambient = 0.1 * u_lightColor;
				vec3 lightDir = normalize(u_lightPos - v_Position);
				float diff = max(dot(norm, lightDir), 0.0);
				vec3 diffuse = diff * u_lightColor;

				vec3 viewDir = normalize(u_viewPos - v_Position);
				vec3 reflectDir = reflect(-lightDir, norm);
				float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
				vec3 specular = 0.5 * spec * u_lightColor;

				float shadow = 1.0;
				if (u_cascadeCount > 0)
					shadow = ShadowFactor(v_Position, norm);

				vec3 indirect = SampleProbeGI(v_Position, norm) * u_objectColor;
				if (u_giDebugMode == 1)
				{
					color = vec4(indirect, 1.0);
					return;
				}
				color = vec4((ambient + (diffuse + specular) * shadow) * u_objectColor + indirect, 1.0);
			}
		)";
		Load("DefaultPhong", vSource, fSource);
	}
#pragma endregion

#pragma region matcap with CSM shadow
	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Normal;
			out vec3 v_Normal;
			out vec3 v_Position;
			uniform mat4 u_Model;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			void main()
			{
				vec4 worldPos = u_Model * vec4(a_Position, 1.0);
				v_Position = worldPos.xyz;
				mat3 normalMatrix = transpose(inverse(mat3(u_View * u_Model)));
				v_Normal = normalize(normalMatrix * a_Normal);
				gl_Position = u_Projection * u_View * worldPos;
			}
		)";

		auto fSource = R"(
			#version 330 core
			in vec3 v_Normal;
			in vec3 v_Position;
			layout(location = 0) out vec4 color;
			layout(location = 1) out vec4 gPosition;
			layout(location = 2) out vec4 gNormal;
			uniform sampler2D u_matcapTex;
			uniform mat4 u_View;
			uniform vec3 u_viewPos;
			uniform int u_cascadeCount;
			uniform float u_cascadeDistances[4];
			uniform mat4 u_lightViewProj[4];
			uniform sampler2DArray u_shadowMap;
			uniform float u_shadowMapSize;
			uniform int u_debugCascadeView;

			vec3 CascadeColor(int cascade)
			{
				if (cascade == 0) return vec3(1.0, 0.2, 0.2);
				if (cascade == 1) return vec3(0.2, 1.0, 0.2);
				if (cascade == 2) return vec3(0.2, 0.4, 1.0);
				return vec3(1.0, 1.0, 0.2);
			}

			int SelectCascade(float viewDepth)
			{
				viewDepth = max(viewDepth, 0.0);
				for (int i = 0; i < u_cascadeCount - 1; i++)
					if (viewDepth <= u_cascadeDistances[i + 1]) return i;
				return u_cascadeCount - 1;
			}

			float ShadowVisibility(vec3 uvz, int cascade, float bias)
			{
				vec2 texelSize = vec2(1.0 / u_shadowMapSize);
				float visible = 0.0;
				for (int y = -1; y <= 1; y++)
				{
					for (int x = -1; x <= 1; x++)
					{
						float depth = texture(u_shadowMap, vec3(uvz.xy + vec2(x, y) * texelSize, cascade)).r;
						visible += (uvz.z - bias > depth) ? 0.3 : 1.0;
					}
				}
				return visible / 9.0;
			}

			float ShadowFactor(vec3 worldPos, vec3 normal)
			{
				vec4 viewPos = u_View * vec4(worldPos, 1.0);
				float viewDepth = max(-viewPos.z, 0.0);
				int cascade = SelectCascade(viewDepth);
				vec4 lightClip = u_lightViewProj[cascade] * vec4(worldPos, 1.0);
				vec3 lightNDC = lightClip.xyz / lightClip.w;
				if (lightNDC.x < -1.0 || lightNDC.x > 1.0 ||
				    lightNDC.y < -1.0 || lightNDC.y > 1.0 ||
				    lightNDC.z < -1.0 || lightNDC.z > 1.0) return 1.0;
				vec3 uvz = lightNDC * 0.5 + 0.5;
				float bias = 0.002;
				return ShadowVisibility(uvz, cascade, bias);
			}

			void main()
			{
				gPosition = vec4(v_Position, 1.0);
				gNormal = vec4(normalize(v_Normal), 1.0);
				if (u_debugCascadeView != 0)
				{
					vec4 viewPos = u_View * vec4(v_Position, 1.0);
					int cascade = SelectCascade(max(-viewPos.z, 0.0));
					color = vec4(CascadeColor(cascade) * 0.85 + vec3(0.05), 1.0);
					return;
				}

				vec3 n = normalize(v_Normal);
				vec2 uv = n.xy * 0.5 + 0.5;
				vec3 scolor = texture(u_matcapTex, uv).rgb;
				float shadow = 1.0;
				if (u_cascadeCount > 0)
					shadow = ShadowFactor(v_Position, n);
				color = vec4(scolor * mix(0.3, 1.0, shadow), 1.0);
			}
		)";
		Load("DefaultMatcap", vSource, fSource);
	}
#pragma endregion

#pragma region background_sh_shader
	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			out vec3 vDir;
			uniform mat4 u_invViewProj;
			void main()
			{
				vec4 worldPos = u_invViewProj * vec4(a_Position, 1.0);
				worldPos /= worldPos.w;
				vDir = normalize(worldPos.xyz);
				gl_Position = vec4(a_Position, 1.0);
			}
		)";

		auto fSource = R"(
			#version 330 core
			in vec3 vDir;
			layout(location = 0) out vec4 FragColor;
			layout(location = 1) out vec4 gPosition;
			layout(location = 2) out vec4 gNormal;
			uniform vec3 shCoeffs[9];

			vec3 evalSH(vec3 dir)
			{
				float x = dir.x, y = dir.y, z = dir.z;
				vec3 result = vec3(0.0);
				result += shCoeffs[0] * 0.282095;
				result += shCoeffs[1] * (0.488603 * y);
				result += shCoeffs[2] * (0.488603 * z);
				result += shCoeffs[3] * (0.488603 * x);
				result += shCoeffs[4] * (1.092548 * x * y);
				result += shCoeffs[5] * (1.092548 * y * z);
				result += shCoeffs[6] * (0.315392 * (3.0 * z * z - 1.0));
				result += shCoeffs[7] * (1.092548 * x * z);
				result += abs(shCoeffs[8] * (0.546274 * (x * x - y * y)));
				return result;
			}
			void main()
			{
				vec3 dir = normalize(vDir);
				FragColor = vec4(evalSH(dir), 1.0);
				gPosition = vec4(0.0);
				gNormal = vec4(0.0);
			}
		)";
		Load("DefaultBackgroundSH", vSource, fSource);
	}
#pragma endregion

#pragma region depth shader for shadow maps
	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			uniform mat4 u_LightViewProj;
			uniform mat4 u_Model;
			void main() {
				gl_Position = u_LightViewProj * u_Model * vec4(a_Position, 1.0);
			}
		)";
		auto fSource = R"(
			#version 330 core
			void main() {}
		)";
		Load("ShadowDepth", vSource, fSource);
	}
#pragma endregion

}
