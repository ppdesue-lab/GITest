#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct Vector2DLine
{
    glm::vec2 Start = glm::vec2(0.0f);
    glm::vec2 End = glm::vec2(0.0f);
    glm::vec4 Color = glm::vec4(0.92f, 0.95f, 1.0f, 1.0f);
    uint32_t ElementIndex = 0;
    bool DisplayAsArc = false;
};

struct Vector2DArc
{
    glm::vec2 Start = glm::vec2(0.0f);
    glm::vec2 End = glm::vec2(0.0f);
    glm::vec2 Center = glm::vec2(0.0f);
    float Radius = 0.0f;
    float StartAngle = 0.0f;
    float SweepAngle = 0.0f;
    glm::vec4 Color = glm::vec4(0.92f, 0.95f, 1.0f, 1.0f);
    uint32_t ElementIndex = 0;
};

enum class Vector2DPrimitiveType
{
    Line,
    Arc
};

struct Vector2DPrimitive
{
    Vector2DPrimitiveType Type = Vector2DPrimitiveType::Line;
    Vector2DLine Line;
    Vector2DArc Arc;
};

struct Vector2DSubElement
{
    std::string Name;
    std::vector<uint32_t> LineIndices;
    std::vector<uint32_t> PrimitiveIndices;
};

struct Vector2DDocument
{
    std::filesystem::path SourcePath;
    std::string SourceName;
    std::vector<Vector2DLine> Lines;
    std::vector<Vector2DPrimitive> Primitives;
    std::vector<Vector2DSubElement> Elements;

    bool Empty() const { return Lines.empty(); }
};
