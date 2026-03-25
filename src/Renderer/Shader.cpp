#include "stdsfx.h"
#include "Shader.h"
#include "Renderer.h"

#include <Platform/OpenGL/OpenGLShader.h>


std::unordered_map<std::string, Ref<Shader>> ShaderLibrary::m_Shaders;

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
			layout(location = 1) in vec3 a_Color;
			uniform mat4 u_View;
			uniform mat4 u_Projection;
			uniform mat4 u_Model;
			out vec4 v_Color;
			void main()
			{
				v_Color = vec4(a_Color,1.0);
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

}