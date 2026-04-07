#pragma once

#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <base.h>

class Shader
{
public:
    virtual void Bind() const =0;
    virtual void Unbind() const = 0;

    //setting
    virtual void SetInt(const std::string& name, int value) = 0;
    virtual void SetIntArray(const std::string& name, int* values, uint32_t count) = 0;
    virtual void SetVec3Array(const std::string& name, float* values, uint32_t count) = 0;
    virtual void SetFloat(const std::string& name, float value) = 0;
    virtual void SetFloat2(const std::string& name, const glm::vec2& value) = 0;
    virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
    virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;

    virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

    virtual const std::string& GetName() const = 0;

    static Ref<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
    static Ref<Shader> Create(const std::string& path);
};

class ShaderLibrary
{
public:
    ShaderLibrary()
    {
        s_Instance = this;
    }
    void Add(const std::string& name, const Ref<Shader>& shader);
    void Add(const Ref<Shader>& shader);
    Ref<Shader> Load(const std::string& name, const std::string& vSource,const std::string& fSource);
    Ref<Shader> Load(const std::string& name, const std::string& path);
    Ref<Shader> Load(const std::string& path);
    Ref<Shader> Get(const std::string& name="DefaultColor");

    bool Exists(const std::string& name);

    void LoadDefault();
    static ShaderLibrary* Instance() { return s_Instance; };
private:
    static std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	static ShaderLibrary* s_Instance;
};