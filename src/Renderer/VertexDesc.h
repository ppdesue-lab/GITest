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
};

struct VertexTexture : public VertexBase
{
    glm::vec2 TexCoord;
};

struct VertexNormal : public VertexBase
{
    glm::vec3 Normal;
};

struct VertexNormalTexture : public VertexBase
{
    glm::vec3 Normal;
    glm::vec2 TexCoord;
};

