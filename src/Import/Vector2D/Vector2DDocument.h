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
};

struct Vector2DSubElement
{
    std::string Name;
    std::vector<uint32_t> LineIndices;
};

struct Vector2DDocument
{
    std::filesystem::path SourcePath;
    std::string SourceName;
    std::vector<Vector2DLine> Lines;
    std::vector<Vector2DSubElement> Elements;

    bool Empty() const { return Lines.empty(); }
};
