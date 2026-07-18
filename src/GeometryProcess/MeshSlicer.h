#pragma once

#include "MathHelper.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GeometryProcess
{
struct SliceOptions
{
    glm::vec3 Normal = glm::vec3(0.0f, 0.0f, 1.0f);
    float LayerHeight = 0.5f;
    float MergeEpsilon = 0.001f;
};

using SliceContours = std::map<int, std::vector<std::vector<glm::vec3>>>;

class MeshSlicer
{
public:
    struct Triangle
    {
        glm::vec3 V0{};
        glm::vec3 V1{};
        glm::vec3 V2{};
    };

    explicit MeshSlicer(SliceOptions options = {})
        : m_Options(options)
    {
        if (glm::length2(m_Options.Normal) <= 0.0f)
            m_Options.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
        m_Options.Normal = glm::normalize(m_Options.Normal);
        m_Options.LayerHeight = std::max(m_Options.LayerHeight, 0.0001f);
        m_Options.MergeEpsilon = std::max(m_Options.MergeEpsilon, 0.00001f);
    }

    SliceContours SliceTriangleVertices(const std::vector<glm::vec3>& triangleVertices) const
    {
        std::vector<Triangle> triangles;
        triangles.reserve(triangleVertices.size() / 3);
        for (std::size_t i = 0; i + 2 < triangleVertices.size(); i += 3)
            triangles.push_back({ triangleVertices[i], triangleVertices[i + 1], triangleVertices[i + 2] });
        return SliceTriangles(triangles);
    }

    SliceContours SliceTriangles(const std::vector<Triangle>& triangles) const
    {
        SliceContours contours;
        if (triangles.empty())
            return contours;

        const glm::mat3 toSliceBasis = createBasis(m_Options.Normal);
        const glm::mat3 toWorldBasis = glm::inverse(toSliceBasis);

        std::vector<Triangle> basisTriangles;
        basisTriangles.reserve(triangles.size());
        glm::vec3 minimum((std::numeric_limits<float>::max)());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());

        for (const Triangle& triangle : triangles)
        {
            Triangle transformed{
                toSliceBasis * triangle.V0,
                toSliceBasis * triangle.V1,
                toSliceBasis * triangle.V2
            };

            if (IsDegenerate(transformed))
                continue;

            ExtendBounds(transformed.V0, minimum, maximum);
            ExtendBounds(transformed.V1, minimum, maximum);
            ExtendBounds(transformed.V2, minimum, maximum);
            basisTriangles.push_back(transformed);
        }

        if (basisTriangles.empty() || maximum.z <= minimum.z)
            return contours;

        const float modelScale = glm::length(maximum - minimum);
        const float mergeEpsilon = std::max(m_Options.MergeEpsilon, modelScale * 0.000001f);
        const std::vector<float> planes = BuildPlanes(minimum.z, maximum.z);

        for (std::size_t layerIndex = 0; layerIndex < planes.size(); ++layerIndex)
        {
            const float planeZ = planes[layerIndex];
            std::vector<Segment> segments;
            segments.reserve(basisTriangles.size());

            for (const Triangle& triangle : basisTriangles)
            {
                Segment segment;
                if (IntersectTriangle(triangle, planeZ, segment, mergeEpsilon))
                    segments.push_back(segment);
            }

            if (segments.empty())
                continue;

            std::vector<std::vector<glm::vec3>> layerContours = BuildContours(segments, mergeEpsilon);
            OrientClosedContours(layerContours, mergeEpsilon);

            for (std::vector<glm::vec3>& contour : layerContours)
            {
                for (glm::vec3& point : contour)
                    point = toWorldBasis * point;
                if (contour.size() >= 2)
                    contours[(int)layerIndex].push_back(std::move(contour));
            }
        }

        return contours;
    }

private:
    struct Segment
    {
        glm::vec3 A{};
        glm::vec3 B{};
    };

    struct VertexKey
    {
        int X = 0;
        int Y = 0;
        int Z = 0;

        bool operator==(const VertexKey& other) const
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }
    };

    struct VertexKeyHash
    {
        std::size_t operator()(const VertexKey& key) const noexcept
        {
            std::size_t seed = std::hash<int>()(key.X);
            seed ^= std::hash<int>()(key.Y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<int>()(key.Z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct VertexNode
    {
        glm::vec3 Position{};
        std::vector<VertexKey> Neighbors;
    };

    static void ExtendBounds(const glm::vec3& point, glm::vec3& minimum, glm::vec3& maximum)
    {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }

    static bool IsDegenerate(const Triangle& triangle)
    {
        constexpr float kMinimumEdgeLength2 = 0.000000000001f;
        if (glm::length2(triangle.V0 - triangle.V1) <= kMinimumEdgeLength2)
            return true;
        if (glm::length2(triangle.V1 - triangle.V2) <= kMinimumEdgeLength2)
            return true;
        if (glm::length2(triangle.V2 - triangle.V0) <= kMinimumEdgeLength2)
            return true;
        return glm::length2(glm::cross(triangle.V1 - triangle.V0, triangle.V2 - triangle.V0)) <= kMinimumEdgeLength2;
    }

    std::vector<float> BuildPlanes(float minimumZ, float maximumZ) const
    {
        std::vector<float> planes;
        for (float z = minimumZ + m_Options.LayerHeight; z < maximumZ; z += m_Options.LayerHeight)
            planes.push_back(z);
        return planes;
    }

    static bool IntersectTriangle(const Triangle& triangle, float planeZ, Segment& outSegment, float epsilon)
    {
        const float minZ = std::min({ triangle.V0.z, triangle.V1.z, triangle.V2.z });
        const float maxZ = std::max({ triangle.V0.z, triangle.V1.z, triangle.V2.z });
        if (!(minZ < planeZ && maxZ > planeZ))
            return false;

        glm::vec3 intersections[2]{};
        int intersectionCount = 0;

        auto addIntersection = [&](const glm::vec3& a, const glm::vec3& b)
        {
            const float edgeMinZ = std::min(a.z, b.z);
            const float edgeMaxZ = std::max(a.z, b.z);

            // Match the reference slicer rule: an edge is closed at its lower
            // endpoint and open at its upper endpoint. This prevents duplicate
            // intersections when a plane passes exactly through a mesh vertex.
            if (!(edgeMinZ <= planeZ && edgeMaxZ > planeZ))
                return;

            const float dz = b.z - a.z;
            if (std::fabs(dz) <= epsilon)
                return;

            const float alpha = (planeZ - a.z) / dz;
            if (alpha < -epsilon || alpha > 1.0f + epsilon)
                return;

            if (intersectionCount < 2)
                intersections[intersectionCount++] = glm::vec3(a.x + alpha * (b.x - a.x), a.y + alpha * (b.y - a.y), planeZ);
        };

        addIntersection(triangle.V0, triangle.V1);
        addIntersection(triangle.V1, triangle.V2);
        addIntersection(triangle.V2, triangle.V0);

        if (intersectionCount != 2)
            return false;
        if (glm::length2(intersections[0] - intersections[1]) <= epsilon * epsilon)
            return false;

        outSegment = { intersections[0], intersections[1] };
        return true;
    }

    static VertexKey MakeKey(const glm::vec3& point, float epsilon)
    {
        return {
            (int)std::lround(point.x / epsilon),
            (int)std::lround(point.y / epsilon),
            (int)std::lround(point.z / epsilon)
        };
    }

    static glm::vec3 KeyPosition(const VertexKey& key, float epsilon)
    {
        return glm::vec3((float)key.X * epsilon, (float)key.Y * epsilon, (float)key.Z * epsilon);
    }

    static bool RemoveNeighbor(std::vector<VertexKey>& neighbors, const VertexKey& key)
    {
        auto it = std::find(neighbors.begin(), neighbors.end(), key);
        if (it == neighbors.end())
            return false;
        neighbors.erase(it);
        return true;
    }

    static bool PopAnyEdge(std::unordered_map<VertexKey, VertexNode, VertexKeyHash>& graph, VertexKey& a, VertexKey& b)
    {
        for (auto& [key, node] : graph)
        {
            if (node.Neighbors.empty())
                continue;

            a = key;
            b = node.Neighbors.back();
            node.Neighbors.pop_back();

            auto reverseIt = graph.find(b);
            if (reverseIt != graph.end())
                RemoveNeighbor(reverseIt->second.Neighbors, a);
            return true;
        }
        return false;
    }

    static void ExtendChain(std::vector<VertexKey>& chain, std::unordered_map<VertexKey, VertexNode, VertexKeyHash>& graph)
    {
        while (!chain.empty())
        {
            const VertexKey current = chain.back();
            auto it = graph.find(current);
            if (it == graph.end() || it->second.Neighbors.empty())
                return;

            const VertexKey next = it->second.Neighbors.back();
            it->second.Neighbors.pop_back();
            auto nextIt = graph.find(next);
            if (nextIt != graph.end())
                RemoveNeighbor(nextIt->second.Neighbors, current);

            chain.push_back(next);
            if (next == chain.front())
                return;
        }
    }

    static std::vector<std::vector<glm::vec3>> BuildContours(const std::vector<Segment>& segments, float epsilon)
    {
        std::unordered_map<VertexKey, VertexNode, VertexKeyHash> graph;
        graph.reserve(segments.size() * 2);

        auto addEdge = [&](const glm::vec3& a, const glm::vec3& b)
        {
            const VertexKey aKey = MakeKey(a, epsilon);
            const VertexKey bKey = MakeKey(b, epsilon);
            if (aKey == bKey)
                return;

            auto& aNode = graph[aKey];
            aNode.Position = KeyPosition(aKey, epsilon);
            aNode.Neighbors.push_back(bKey);

            auto& bNode = graph[bKey];
            bNode.Position = KeyPosition(bKey, epsilon);
            bNode.Neighbors.push_back(aKey);
        };

        for (const Segment& segment : segments)
            addEdge(segment.A, segment.B);

        std::vector<std::vector<glm::vec3>> contours;
        VertexKey a;
        VertexKey b;
        while (PopAnyEdge(graph, a, b))
        {
            std::vector<VertexKey> chain{ a, b };
            ExtendChain(chain, graph);

            if (!(chain.size() > 2 && chain.front() == chain.back()))
            {
                std::reverse(chain.begin(), chain.end());
                ExtendChain(chain, graph);
            }

            std::vector<glm::vec3> contour;
            contour.reserve(chain.size());
            for (const VertexKey& key : chain)
            {
                auto it = graph.find(key);
                contour.push_back(it != graph.end() ? it->second.Position : KeyPosition(key, epsilon));
            }

            if (contour.size() >= 2)
                contours.push_back(std::move(contour));
        }

        return contours;
    }

    static float SignedArea(const std::vector<glm::vec3>& contour)
    {
        if (contour.size() < 3)
            return 0.0f;

        float area = 0.0f;
        for (std::size_t i = 0; i < contour.size(); ++i)
        {
            const glm::vec3& a = contour[i];
            const glm::vec3& b = contour[(i + 1) % contour.size()];
            area += a.x * b.y - a.y * b.x;
        }
        return area * 0.5f;
    }

    static bool IsClosed(const std::vector<glm::vec3>& contour, float epsilon)
    {
        return contour.size() > 3 && glm::length2(contour.front() - contour.back()) <= epsilon * epsilon;
    }

    static bool ContainsPoint2D(const std::vector<glm::vec3>& contour, const glm::vec3& point)
    {
        bool inside = false;
        for (std::size_t i = 0, j = contour.size() - 1; i < contour.size(); j = i++)
        {
            const glm::vec3& a = contour[i];
            const glm::vec3& b = contour[j];
            const bool intersects = ((a.y > point.y) != (b.y > point.y))
                && (point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x);
            if (intersects)
                inside = !inside;
        }
        return inside;
    }

    static void OrientClosedContours(std::vector<std::vector<glm::vec3>>& contours, float epsilon)
    {
        for (std::size_t i = 0; i < contours.size(); ++i)
        {
            if (!IsClosed(contours[i], epsilon))
                continue;

            int depth = 0;
            const glm::vec3 sample = contours[i].front();
            for (std::size_t j = 0; j < contours.size(); ++j)
            {
                if (i == j || !IsClosed(contours[j], epsilon))
                    continue;
                if (ContainsPoint2D(contours[j], sample))
                    ++depth;
            }

            const bool shouldBeCounterClockwise = (depth % 2) == 0;
            const bool isCounterClockwise = SignedArea(contours[i]) > 0.0f;
            if (shouldBeCounterClockwise != isCounterClockwise)
                std::reverse(contours[i].begin(), contours[i].end());
        }
    }

private:
    SliceOptions m_Options;
};

inline SliceContours SliceTriangleVertices(const std::vector<glm::vec3>& triangleVertices, const SliceOptions& options = {})
{
    return MeshSlicer(options).SliceTriangleVertices(triangleVertices);
}

inline SliceContours SliceTriangles(const std::vector<MeshSlicer::Triangle>& triangles, const SliceOptions& options = {})
{
    return MeshSlicer(options).SliceTriangles(triangles);
}
}
