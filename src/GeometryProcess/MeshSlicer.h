#pragma once

#include "slicerGLM.hpp"

#include <map>
#include <string>
#include <vector>

namespace GeometryProcess
{
struct SliceOptions
{
    glm::vec3 Normal = glm::vec3(0.0f, 0.0f, 1.0f);
    float LayerHeight = 0.1f;
};

using SliceContours = std::map<int, std::vector<std::vector<glm::vec3>>>;

inline SliceContours SliceTriangles(const std::vector<slicing::Triangle>& triangles, const SliceOptions& options = {})
{
    slicing::Slicer slicer(triangles, {}, options.LayerHeight, true);
    slicer.incremental_slicing();

    SliceContours result;
    const auto& planePolygons = slicer.planes_with_polygons();
    for (std::size_t i = 0; i < planePolygons.size(); ++i)
    {
        for (const auto& polygon : planePolygons[i])
            result[(int)i].push_back(::transformPointsBasisBack(polygon.vertices, options.Normal));
    }
    return result;
}

inline SliceContours SliceTriangleVertices(const std::vector<glm::vec3>& triangleVertices, const SliceOptions& options = {})
{
    std::vector<glm::vec3> transformed = ::transformPointsToBasis(triangleVertices, options.Normal);
    std::vector<slicing::Triangle> triangles;
    triangles.reserve(transformed.size() / 3);
    for (std::size_t i = 0; i + 2 < transformed.size(); i += 3)
        triangles.push_back({ transformed[i], transformed[i + 1], transformed[i + 2] });
    return SliceTriangles(triangles, options);
}

inline SliceContours SliceModelFile(const std::string& filepath, const SliceOptions& options = {})
{
    return slicing::LoadModelAndMakeSlices(filepath, options.Normal, options.LayerHeight);
}
}
