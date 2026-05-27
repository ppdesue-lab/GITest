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
				gl_FragDepth = max(gl_FragCoord.z - 0.000002, 0.0);
			}
		)";
		Load("DefaultLineColor", vSource, fSource);
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
			uniform float u_shadowConstantBias;
			uniform float u_shadowSlopeBias;
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
				float bias = max(u_shadowSlopeBias * (1.0 - dot(normalize(normal), normalize(-u_lightDir))),
					u_shadowConstantBias);
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

#pragma region pmx_toon_shader
	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Normal;
			layout(location = 2) in vec2 a_TexCoord;
			uniform mat4 u_Model;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			out vec3 v_Position;
			out vec3 v_Normal;
			out vec2 v_TexCoord;
			void main()
			{
				vec4 worldPos = u_Model * vec4(a_Position, 1.0);
				v_Position = worldPos.xyz;
				v_Normal = mat3(transpose(inverse(u_Model))) * a_Normal;
				v_TexCoord = a_TexCoord;
				gl_Position = u_Projection * u_View * worldPos;
			}
		)";
		auto fSource = R"(
			#version 330 core
			layout(location = 0) out vec4 color;
			layout(location = 1) out vec4 gPosition;
			layout(location = 2) out vec4 gNormal;
			in vec3 v_Position;
			in vec3 v_Normal;
			in vec2 v_TexCoord;
			uniform mat4 u_View;
			uniform vec3 u_viewPos;
			uniform vec3 u_lightDir;
			uniform vec3 u_lightColor;
			uniform vec3 u_Diffuse;
			uniform vec3 u_Ambient;
			uniform vec3 u_Specular;
			uniform float u_SpecularPower;
			uniform float u_Alpha;
			uniform sampler2D u_MainTexture;
			uniform sampler2D u_SphereTexture;
			uniform sampler2D u_ToonTexture;
			uniform int u_HasMainTexture;
			uniform int u_HasSphereTexture;
			uniform int u_HasToonTexture;
			uniform int u_SphereMode;
			uniform int u_cascadeCount;
			uniform float u_cascadeDistances[4];
			uniform mat4 u_lightViewProj[4];
			uniform sampler2DArray u_shadowMap;
			uniform float u_shadowMapSize;
			uniform float u_shadowConstantBias;
			uniform float u_shadowSlopeBias;
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
				for (int i = 0; i < u_cascadeCount - 1; ++i)
					if (viewDepth <= u_cascadeDistances[i + 1]) return i;
				return max(u_cascadeCount - 1, 0);
			}
			float ShadowFactor(vec3 worldPos, vec3 normal)
			{
				if (u_cascadeCount <= 0)
					return 1.0;
				float viewDepth = max(-(u_View * vec4(worldPos, 1.0)).z, 0.0);
				int cascade = SelectCascade(viewDepth);
				vec4 lightClip = u_lightViewProj[cascade] * vec4(worldPos, 1.0);
				vec3 ndc = lightClip.xyz / lightClip.w;
				if (any(greaterThan(abs(ndc), vec3(1.0))))
					return 1.0;
				vec3 uvz = ndc * 0.5 + 0.5;
				float bias = max(u_shadowSlopeBias * (1.0 - dot(normal, normalize(-u_lightDir))),
					u_shadowConstantBias);
				float visibility = 0.0;
				vec2 texel = vec2(1.0 / u_shadowMapSize);
				for (int y = -1; y <= 1; ++y)
					for (int x = -1; x <= 1; ++x)
					{
						float depth = texture(u_shadowMap, vec3(uvz.xy + vec2(x, y) * texel, cascade)).r;
						visibility += uvz.z - bias > depth ? 0.35 : 1.0;
					}
				return visibility / 9.0;
			}
			void main()
			{
				vec3 n = normalize(v_Normal);
				gPosition = vec4(v_Position, 1.0);
				gNormal = vec4(n, 1.0);
				if (u_debugCascadeView != 0)
				{
					int cascade = SelectCascade(max(-(u_View * vec4(v_Position, 1.0)).z, 0.0));
					color = vec4(CascadeColor(cascade) * 0.85 + vec3(0.05), 1.0);
					return;
				}

				vec3 lightDir = normalize(-u_lightDir);
				vec3 viewDir = normalize(u_viewPos - v_Position);
				vec3 halfDir = normalize(lightDir + viewDir);
				float toonAmount = clamp(dot(n, lightDir) + 0.5, 0.0, 1.0);
				float shadow = ShadowFactor(v_Position, n);
				toonAmount *= mix(0.45, 1.0, shadow);

				vec4 texColor = u_HasMainTexture != 0 ? texture(u_MainTexture, v_TexCoord) : vec4(1.0);
				float alpha = u_Alpha * texColor.a;
				if (alpha <= 0.001)
					discard;

				vec3 baseColor = clamp(u_Diffuse * u_lightColor + u_Ambient, vec3(0.0), vec3(1.0));
				baseColor *= texColor.rgb;
				vec3 toonColor;
				if (u_HasToonTexture != 0)
					toonColor = texture(u_ToonTexture, vec2(0.5, 1.0 - toonAmount)).rgb;
				else
				{
					float band = toonAmount > 0.72 ? 1.0 : (toonAmount > 0.42 ? 0.72 : 0.48);
					toonColor = vec3(band);
				}
				vec3 result = baseColor * toonColor;
				if (u_HasSphereTexture != 0)
				{
					vec3 viewNormal = normalize(mat3(u_View) * n);
					vec3 sphereColor = texture(u_SphereTexture, viewNormal.xy * 0.5 + 0.5).rgb;
					result = u_SphereMode == 2 ? result + sphereColor : result * sphereColor;
				}
				float specularFactor = pow(max(dot(n, halfDir), 0.0), max(u_SpecularPower, 1.0));
				result += u_Specular * u_lightColor * specularFactor * shadow;
				color = vec4(result, alpha);
			}
		)";
		Load("DefaultToon", vSource, fSource);
	}

	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Normal;
			uniform mat4 u_Model;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			uniform vec2 u_ScreenSize;
			uniform float u_EdgeSize;
			void main()
			{
				vec4 worldPos = u_Model * vec4(a_Position, 1.0);
				vec4 clipPos = u_Projection * u_View * worldPos;
				vec3 viewNormal = normalize(mat3(transpose(inverse(u_View * u_Model))) * a_Normal);
				vec2 screenNormal = viewNormal.xy;
				float normalLength = length(screenNormal);
				if (normalLength > 0.0001)
					screenNormal /= normalLength;
				else
					screenNormal = vec2(0.0);
				clipPos.xy += screenNormal * (u_EdgeSize * 2.0 / max(u_ScreenSize, vec2(1.0))) * clipPos.w;
				gl_Position = clipPos;
			}
		)";
		auto fSource = R"(
			#version 330 core
			layout(location = 0) out vec4 color;
			layout(location = 1) out vec4 gPosition;
			layout(location = 2) out vec4 gNormal;
			uniform vec4 u_EdgeColor;
			void main()
			{
				color = u_EdgeColor;
				gPosition = vec4(0.0);
				gNormal = vec4(0.0);
			}
		)";
		Load("DefaultToonEdge", vSource, fSource);
	}
#pragma endregion

#pragma region pbr_ibl_shader
	{
		auto vSource = R"(
			#version 330 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Normal;
			layout(location = 2) in vec2 a_TexCoord;
			uniform mat4 u_Model;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			out vec3 v_Position;
			out vec3 v_Normal;
			out vec2 v_TexCoord;
			void main()
			{
				vec4 worldPos = u_Model * vec4(a_Position, 1.0);
				v_Position = worldPos.xyz;
				v_Normal = mat3(transpose(inverse(u_Model))) * a_Normal;
				v_TexCoord = a_TexCoord;
				gl_Position = u_Projection * u_View * worldPos;
			}
		)";
		auto fSource = R"(
			#version 330 core
			layout(location = 0) out vec4 color;
			layout(location = 1) out vec4 gPosition;
			layout(location = 2) out vec4 gNormal;
			in vec3 v_Position;
			in vec3 v_Normal;
			in vec2 v_TexCoord;
			uniform mat4 u_View;
			uniform vec3 u_viewPos;
			uniform vec3 u_lightDir;
			uniform vec3 u_lightColor;
			uniform vec3 u_Albedo;
			uniform float u_Metallic;
			uniform float u_Roughness;
			uniform float u_AO;
			uniform sampler2D u_AlbedoMap;
			uniform sampler2D u_NormalMap;
			uniform sampler2D u_MetallicMap;
			uniform sampler2D u_RoughnessMap;
			uniform sampler2D u_AOMap;
			uniform int u_HasAlbedoMap;
			uniform int u_HasNormalMap;
			uniform int u_HasMetallicMap;
			uniform int u_HasRoughnessMap;
			uniform int u_HasAOMap;
			uniform int u_MetallicMapChannel;
			uniform int u_RoughnessMapChannel;
			uniform int u_AOMapChannel;
			uniform samplerCube u_IrradianceMap;
			uniform samplerCube u_PrefilterMap;
			uniform sampler2D u_BRDFLUT;
			uniform int u_iblEnabled;
			uniform int u_iblDiffuseEnabled;
			uniform int u_iblSpecularEnabled;
			uniform float u_iblDiffuseIntensity;
			uniform float u_iblSpecularIntensity;
			uniform int u_pbrDebugMode;
			uniform int u_cascadeCount;
			uniform float u_cascadeDistances[4];
			uniform mat4 u_lightViewProj[4];
			uniform sampler2DArray u_shadowMap;
			uniform float u_shadowMapSize;
			uniform float u_shadowConstantBias;
			uniform float u_shadowSlopeBias;
			uniform int u_debugCascadeView;
			uniform int u_giEnabled;
			uniform int u_giDebugMode;
			uniform float u_giIntensity;
			uniform vec3 u_giOrigin;
			uniform float u_giSpacing;
			uniform vec3 u_giCounts;
			uniform vec3 u_probeIrradiance[64];
			const float PI = 3.14159265359;
			vec3 CascadeColor(int cascade)
			{
				if (cascade == 0) return vec3(1.0, 0.2, 0.2);
				if (cascade == 1) return vec3(0.2, 1.0, 0.2);
				if (cascade == 2) return vec3(0.2, 0.4, 1.0);
				return vec3(1.0, 1.0, 0.2);
			}
			int SelectCascade(float viewDepth)
			{
				for (int i = 0; i < u_cascadeCount - 1; ++i)
					if (viewDepth <= u_cascadeDistances[i + 1]) return i;
				return max(u_cascadeCount - 1, 0);
			}
			float ShadowFactor(vec3 worldPos, vec3 normal)
			{
				if (u_cascadeCount <= 0)
					return 1.0;
				float viewDepth = max(-(u_View * vec4(worldPos, 1.0)).z, 0.0);
				int cascade = SelectCascade(viewDepth);
				vec4 lightClip = u_lightViewProj[cascade] * vec4(worldPos, 1.0);
				vec3 ndc = lightClip.xyz / lightClip.w;
				if (any(greaterThan(abs(ndc), vec3(1.0))))
					return 1.0;
				vec3 uvz = ndc * 0.5 + 0.5;
				float bias = max(u_shadowSlopeBias * (1.0 - dot(normal, normalize(-u_lightDir))),
					u_shadowConstantBias);
				float visibility = 0.0;
				vec2 texel = vec2(1.0 / u_shadowMapSize);
				for (int y = -1; y <= 1; ++y)
					for (int x = -1; x <= 1; ++x)
					{
						float depth = texture(u_shadowMap, vec3(uvz.xy + vec2(x, y) * texel, cascade)).r;
						visibility += uvz.z - bias > depth ? 0.3 : 1.0;
					}
				return visibility / 9.0;
			}
			int ProbeIndex(ivec3 c, ivec3 counts)
			{
				return c.x + counts.x * (c.y + counts.y * c.z);
			}
			vec3 SampleProbeGI(vec3 worldPos, vec3 normal, vec3 albedo, float metallic)
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
				vec3 irradiance = mix(low, high, f.z) * diffuseResponse * u_giIntensity;
				return irradiance * albedo * (1.0 - metallic);
			}
			float DistributionGGX(vec3 n, vec3 h, float roughness)
			{
				float a = roughness * roughness;
				float a2 = a * a;
				float ndoth = max(dot(n, h), 0.0);
				float denominator = ndoth * ndoth * (a2 - 1.0) + 1.0;
				return a2 / max(PI * denominator * denominator, 0.0001);
			}
			float GeometrySchlickGGX(float ndotv, float roughness)
			{
				float r = roughness + 1.0;
				float k = (r * r) / 8.0;
				return ndotv / (ndotv * (1.0 - k) + k);
			}
			float GeometrySmith(vec3 n, vec3 v, vec3 l, float roughness)
			{
				return GeometrySchlickGGX(max(dot(n, v), 0.0), roughness) *
					GeometrySchlickGGX(max(dot(n, l), 0.0), roughness);
			}
			vec3 FresnelSchlick(float cosTheta, vec3 f0)
			{
				return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
			}
			vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
			{
				return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
					pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
			}
			vec3 GetNormal()
			{
				vec3 n = normalize(v_Normal);
				if (u_HasNormalMap == 0)
					return n;
				vec3 tangentNormal = texture(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;
				vec3 dp1 = dFdx(v_Position);
				vec3 dp2 = dFdy(v_Position);
				vec2 duv1 = dFdx(v_TexCoord);
				vec2 duv2 = dFdy(v_TexCoord);
				vec3 t = normalize(dp1 * duv2.y - dp2 * duv1.y);
				vec3 b = normalize(-cross(n, t));
				return normalize(mat3(t, b, n) * tangentNormal);
			}
			float SampleChannel(sampler2D tex, vec2 uv, int channel)
			{
				vec4 value = texture(tex, uv);
				if (channel == 1) return value.g;
				if (channel == 2) return value.b;
				if (channel == 3) return value.a;
				return value.r;
			}
			void main()
			{
				vec3 albedo = u_HasAlbedoMap != 0
					? pow(texture(u_AlbedoMap, v_TexCoord).rgb, vec3(2.2)) * u_Albedo : u_Albedo;
				float roughness = clamp(u_HasRoughnessMap != 0
					? SampleChannel(u_RoughnessMap, v_TexCoord, u_RoughnessMapChannel) : u_Roughness, 0.04, 1.0);
				float metallic = clamp(u_HasMetallicMap != 0
					? SampleChannel(u_MetallicMap, v_TexCoord, u_MetallicMapChannel) : u_Metallic, 0.0, 1.0);
				float ao = clamp(u_HasAOMap != 0
					? SampleChannel(u_AOMap, v_TexCoord, u_AOMapChannel) : u_AO, 0.0, 1.0);
				vec3 n = GetNormal();
				gPosition = vec4(v_Position, 1.0);
				gNormal = vec4(n, 1.0);
				if (u_pbrDebugMode == 1)
				{
					color = vec4(pow(max(albedo, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
					return;
				}
				if (u_pbrDebugMode == 2)
				{
					color = vec4(n * 0.5 + 0.5, 1.0);
					return;
				}
				if (u_debugCascadeView != 0)
				{
					int cascade = SelectCascade(max(-(u_View * vec4(v_Position, 1.0)).z, 0.0));
					color = vec4(CascadeColor(cascade) * 0.85 + vec3(0.05), 1.0);
					return;
				}
				vec3 v = normalize(u_viewPos - v_Position);
				vec3 r = reflect(-v, n);
				vec3 f0 = mix(vec3(0.04), albedo, metallic);
				vec3 l = normalize(-u_lightDir);
				vec3 h = normalize(v + l);
				float ndotl = max(dot(n, l), 0.0);
				float ndotv = max(dot(n, v), 0.0);
				float ndf = DistributionGGX(n, h, roughness);
				float geometry = GeometrySmith(n, v, l, roughness);
				vec3 fresnel = FresnelSchlick(max(dot(h, v), 0.0), f0);
				vec3 specular = (ndf * geometry * fresnel) / max(4.0 * ndotv * ndotl, 0.001);
				vec3 kd = (vec3(1.0) - fresnel) * (1.0 - metallic);
				vec3 direct = (kd * albedo / PI + specular) * u_lightColor * ndotl;
				vec3 iblF = FresnelSchlickRoughness(ndotv, f0, roughness);
				vec3 iblKD = (vec3(1.0) - iblF) * (1.0 - metallic);
				vec3 diffuse = texture(u_IrradianceMap, n).rgb * albedo;
				vec3 prefiltered = textureLod(u_PrefilterMap, r, roughness * 4.0).rgb;
				vec2 brdf = texture(u_BRDFLUT, vec2(ndotv, roughness)).rg;
				vec3 diffuseIBL = iblKD * diffuse * u_iblDiffuseIntensity;
				vec3 specularIBL = prefiltered * (iblF * brdf.x + brdf.y) * u_iblSpecularIntensity;
				if (u_iblEnabled == 0 || u_iblDiffuseEnabled == 0)
					diffuseIBL = vec3(0.0);
				if (u_iblEnabled == 0 || u_iblSpecularEnabled == 0)
					specularIBL = vec3(0.0);
				if (u_pbrDebugMode == 3 || u_pbrDebugMode == 4)
				{
					vec3 debugIBL = u_pbrDebugMode == 3 ? diffuseIBL : specularIBL;
					debugIBL = debugIBL / (debugIBL + vec3(1.0));
					color = vec4(pow(debugIBL, vec3(1.0 / 2.2)), 1.0);
					return;
				}
				vec3 ambient = (diffuseIBL + specularIBL) * ao;
				vec3 indirect = SampleProbeGI(v_Position, n, albedo, metallic);
				if (u_giDebugMode == 1)
				{
					vec3 debugIndirect = indirect / (indirect + vec3(1.0));
					color = vec4(pow(debugIndirect, vec3(1.0 / 2.2)), 1.0);
					return;
				}
				vec3 result = ambient + indirect + direct * ShadowFactor(v_Position, n);
				result = result / (result + vec3(1.0));
				result = pow(result, vec3(1.0 / 2.2));
				color = vec4(result, 1.0);
			}
		)";
		Load("DefaultPBR", vSource, fSource);
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
			uniform float u_shadowConstantBias;
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
				float bias = u_shadowConstantBias;
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
			uniform samplerCube u_EnvironmentMap;
			uniform int u_BackgroundMode;

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
				vec3 background = evalSH(dir);
				if (u_BackgroundMode == 1)
				{
					background = texture(u_EnvironmentMap, dir).rgb;
					background = background / (background + vec3(1.0));
					background = pow(background, vec3(1.0 / 2.2));
				}
				FragColor = vec4(background, 1.0);
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
