#include "stdsfx.h"
#include "Shader.h"
#include "Renderer.h"

#include <Platform/OpenGL/OpenGLShader.h>


std::unordered_map<std::string, Ref<Shader>> ShaderLibrary::m_Shaders;
ShaderLibrary* ShaderLibrary::s_Instance;

Ref<Shader> Shader::Create(const std::string& filepath)
{
    switch(Renderer::GetAPI())
    {
        case Renderer::API::None:    ERROR("RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::OpenGL:  return CreateRef<OpenGLShader>(filepath);
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

		//create shader
		Load("DefaultColor", vSource, fSource);
	}


#pragma endregion
#pragma region phong shader with color
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
			in vec3 v_Position;
			in vec3 v_Normal;

			uniform vec3 u_lightPos;
			uniform vec3 u_viewPos;

			uniform vec3 u_lightColor;
			uniform vec3 u_objectColor;
			
			void main()
			{
				// --- 环境光 ---
				float ambientStrength = 0.1;
				vec3 ambient = ambientStrength * u_lightColor;

				// --- 漫反射 ---
				vec3 norm = normalize(v_Normal);
				vec3 lightDir = normalize(u_lightPos - v_Position);

				float diff = max(dot(norm, lightDir), 0.0);
				vec3 diffuse = diff * u_lightColor;

				// --- 镜面反射 ---
				float specularStrength = 0.5;
				vec3 viewDir = normalize(u_viewPos - v_Position);
				vec3 reflectDir = reflect(-lightDir, norm);

				float shininess = 32.0;
				float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

				vec3 specular = specularStrength * spec * u_lightColor;

				// --- 合成 ---
				vec3 result =  (ambient + diffuse + specular) * u_objectColor;
				color = vec4(result, 1.0);
			}
		)";
		//create shader
		Load("DefaultPhong", vSource, fSource);
	}
#pragma endregion

#pragma region matcap
	{
		auto vSource = R"(
			#version 330 core

			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Normal;

			out vec3 v_Normal;

			uniform mat4 u_Model;
			uniform mat4 u_View;
			uniform mat4 u_Projection;

			void main()
			{
				// 转到 view space
				mat3 normalMatrix = transpose(inverse(mat3(u_View * u_Model)));
				v_Normal = normalize(normalMatrix * a_Normal);

				gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
			}
		)";

		auto fSource = R"(
			#version 330 core
			
			in vec3 v_Normal;
			out vec4 color;

			uniform sampler2D u_matcapTex;

			void main()
			{
				vec3 n = normalize(v_Normal);

				// 👇 MatCap UV（关键）
				vec2 uv = n.xy * 0.5 + 0.5;

				vec3 scolor = texture(u_matcapTex, uv).rgb;

				color = vec4(scolor, 1.0);
			}
		)";
		//create shader
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
			out vec4 FragColor;

			// 9个 SH 系数（每个是 vec3 表示 RGB）
			uniform vec3 shCoeffs[9];

			// SH basis（3阶）
			vec3 evalSH(vec3 dir)
			{
				float x = dir.x;
				float y = dir.y;
				float z = dir.z;

				vec3 result = vec3(0.0);

				// 常数项
				result += shCoeffs[0] * 0.282095;

				// 一阶
				result += shCoeffs[1] * (0.488603 * y);
				result += shCoeffs[2] * (0.488603 * z);
				result += shCoeffs[3] * (0.488603 * x);

				// 二阶
				result += shCoeffs[4] * (1.092548 * x * y);
				result += shCoeffs[5] * (1.092548 * y * z);
				result += shCoeffs[6] * (0.315392 * (3.0 * z * z - 1.0));
				result += shCoeffs[7] * (1.092548 * x * z);
				result += shCoeffs[8] * (0.546274 * (x * x - y * y));

				return result;
			}

			void main()
			{
				vec3 dir = normalize(vDir);

				vec3 color = evalSH(dir);

				FragColor = vec4(color, 0.5);
			}
		)";
		//create shader
		Load("DefaultBackgroundSH", vSource, fSource);
	}
#pragma endregion


}