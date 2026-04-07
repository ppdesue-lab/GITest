#pragma once

struct VertexBase
{
	glm::vec3 Position;
protected:
    VertexBase() :Position(glm::vec3(0,0,0)){} //cannot access from outside
};

struct VertexColor : public VertexBase
{
	glm::vec4 Color;

    VertexColor(glm::vec3 position = glm::vec3(0, 0, 0), glm::vec4 color = glm::vec4(1, 1, 1, 1))
        : VertexBase(), Color(color)
    {
        Position = position;
	}
};

struct VertexTexture : public VertexBase
{
    glm::vec2 TexCoord;
    VertexTexture(glm::vec3 position = glm::vec3(0, 0, 0), glm::vec2 texCoord = glm::vec2(0, 0))
        : VertexBase(), TexCoord(texCoord)
    {
        Position = position;
	}
};

struct VertexNormal : public VertexBase
{
    glm::vec3 Normal;
    VertexNormal(glm::vec3 position = glm::vec3(0, 0, 0), glm::vec3 normal = glm::vec3(0, 0, 1))
        : VertexBase(), Normal(normal)
    {
        Position = position;
    }
};

struct VertexNormalTexture : public VertexBase
{
    glm::vec3 Normal;
    glm::vec2 TexCoord;

    VertexNormalTexture(glm::vec3 position = glm::vec3(0, 0, 0), glm::vec3 normal = glm::vec3(0, 0, 1), glm::vec2 texCoord = glm::vec2(0, 0))
        : VertexBase(), Normal(normal), TexCoord(texCoord)
    {
        Position = position;
	}
};

