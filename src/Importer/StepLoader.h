#pragma once

#include <glm/glm.hpp>
#include <Renderer/VertexDesc.h>

#include <filesystem>
#include <string>
#include <vector>

struct StepMeshData
{
    std::vector<VertexNormalTexture> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<glm::vec3> EdgeVertices;
    std::string Error;
};

class StepLoader
{
public:
    static bool Load(const std::filesystem::path& filepath, StepMeshData& mesh);
};
