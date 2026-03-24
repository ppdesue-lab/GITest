#pragma once

#include <Renderer/VertexArray.h>
#include <glm/glm.hpp>
class Primitive
{
public:
	Primitive() = default;
	virtual ~Primitive() = default;
	virtual Ref<VertexArray> GetVertexArray() const= 0;
};

class Axis : public Primitive
{
public:
	Axis(const glm::vec3& default_len=glm::vec3(1,1,1));

	virtual ~Axis() = default;

	Ref<VertexArray> GetVertexArray() const override{ return m_VertexArray;}
    uint32_t GetCount(){ return m_Count;}

    glm::vec3 Length = glm::vec3(1.0f);
private:
    void Create();
private:
	Ref<VertexArray> m_VertexArray;
    uint32_t m_Count = 0;
};