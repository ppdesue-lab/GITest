#include "stdsfx.h"
#include "DxfLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <dime/Base.h>
#include <dime/Input.h>
#include <dime/Model.h>
#include <dime/State.h>
#include <dime/entities/Entity.h>
#include <dime/entities/Spline.h>
#include <dime/util/Array.h>
#include <dime/util/Linear.h>

namespace
{
struct DxfLoadContext
{
    Vector2DDocument* Document = nullptr;
};

glm::vec4 DxfColor(int colorNumber)
{
    static const std::array<glm::vec4, 16> colors = {
        glm::vec4(0.92f, 0.95f, 1.0f, 1.0f),
        glm::vec4(1.00f, 0.18f, 0.18f, 1.0f),
        glm::vec4(1.00f, 0.92f, 0.16f, 1.0f),
        glm::vec4(0.16f, 0.92f, 0.24f, 1.0f),
        glm::vec4(0.16f, 0.90f, 0.92f, 1.0f),
        glm::vec4(0.20f, 0.36f, 1.00f, 1.0f),
        glm::vec4(0.86f, 0.22f, 0.96f, 1.0f),
        glm::vec4(0.86f, 0.86f, 0.86f, 1.0f),
        glm::vec4(0.55f, 0.55f, 0.55f, 1.0f),
        glm::vec4(1.00f, 0.52f, 0.52f, 1.0f),
        glm::vec4(1.00f, 0.86f, 0.45f, 1.0f),
        glm::vec4(0.55f, 1.00f, 0.55f, 1.0f),
        glm::vec4(0.55f, 1.00f, 1.00f, 1.0f),
        glm::vec4(0.56f, 0.68f, 1.00f, 1.0f),
        glm::vec4(1.00f, 0.60f, 1.00f, 1.0f),
        glm::vec4(1.00f, 1.00f, 1.00f, 1.0f),
    };

    if (colorNumber <= 0 || colorNumber >= 256)
        return colors[0];
    return colors[(size_t)colorNumber % colors.size()];
}

glm::vec2 ToVec2(const dimeVec3f& value)
{
    return glm::vec2((float)value.x, (float)value.y);
}

uint32_t BeginElement(Vector2DDocument& document, const std::string& name)
{
    Vector2DSubElement element;
    element.Name = name;
    document.Elements.push_back(std::move(element));
    return (uint32_t)document.Elements.size() - 1;
}

dimeVec3f TransformPoint(const dimeMatrix& matrix, const dimeVec3f& point)
{
    dimeVec3f transformed = point;
    matrix.multMatrixVec(transformed);
    return transformed;
}

void AddLine(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeVec3f& start, const dimeVec3f& end, const glm::vec4& color)
{
    const dimeVec3f transformedStart = TransformPoint(transform, start);
    const dimeVec3f transformedEnd = TransformPoint(transform, end);
    if (std::abs(transformedStart.x - transformedEnd.x) < 0.000001f &&
        std::abs(transformedStart.y - transformedEnd.y) < 0.000001f)
        return;

    const uint32_t lineIndex = (uint32_t)document.Lines.size();
    document.Lines.push_back({ ToVec2(transformedStart), ToVec2(transformedEnd), color, elementIndex });
    if (elementIndex < document.Elements.size())
        document.Elements[elementIndex].LineIndices.push_back(lineIndex);
}

void AddLineStrip(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeArray<dimeVec3f>& vertices, const glm::vec4& color)
{
    for (int i = 1; i < vertices.count(); ++i)
        AddLine(document, elementIndex, transform, vertices[i - 1], vertices[i], color);
}

void AddIndexedLines(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeArray<dimeVec3f>& vertices, const dimeArray<int>& indices, const glm::vec4& color)
{
    int previous = -1;
    for (int i = 0; i < indices.count(); ++i)
    {
        const int current = indices[i];
        if (current < 0 || current >= vertices.count())
        {
            previous = -1;
            continue;
        }

        if (previous >= 0)
            AddLine(document, elementIndex, transform, vertices[previous], vertices[current], color);
        previous = current;
    }
}

int FindKnotSpan(int controlPointCount, int degree, float parameter, const std::vector<float>& knots)
{
    const int lastControlPoint = controlPointCount - 1;
    if (parameter >= knots[lastControlPoint + 1])
        return lastControlPoint;
    if (parameter <= knots[degree])
        return degree;

    int low = degree;
    int high = lastControlPoint + 1;
    int mid = (low + high) / 2;
    while (parameter < knots[mid] || parameter >= knots[mid + 1])
    {
        if (parameter < knots[mid])
            high = mid;
        else
            low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

std::vector<float> BasisFunctions(int span, float parameter, int degree, const std::vector<float>& knots)
{
    std::vector<float> basis((size_t)degree + 1, 0.0f);
    std::vector<float> left((size_t)degree + 1, 0.0f);
    std::vector<float> right((size_t)degree + 1, 0.0f);
    basis[0] = 1.0f;

    for (int j = 1; j <= degree; ++j)
    {
        left[j] = parameter - knots[span + 1 - j];
        right[j] = knots[span + j] - parameter;
        float saved = 0.0f;
        for (int r = 0; r < j; ++r)
        {
            const float denominator = right[r + 1] + left[j - r];
            const float temporary = std::abs(denominator) > 0.000001f ? basis[r] / denominator : 0.0f;
            basis[r] = saved + right[r + 1] * temporary;
            saved = left[j - r] * temporary;
        }
        basis[j] = saved;
    }

    return basis;
}

dimeVec3f EvaluateSpline(const dimeSpline& spline, float parameter, const std::vector<float>& knots)
{
    const int degree = std::max(1, (int)spline.getDegree());
    const int controlPointCount = spline.getNumControlPoints();
    const int span = FindKnotSpan(controlPointCount, degree, parameter, knots);
    const std::vector<float> basis = BasisFunctions(span, parameter, degree, knots);

    dimeVec3f point(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    for (int j = 0; j <= degree; ++j)
    {
        const int controlIndex = span - degree + j;
        if (controlIndex < 0 || controlIndex >= controlPointCount)
            continue;

        const float weight = (float)spline.getWeight(controlIndex);
        const float factor = basis[j] * weight;
        const dimeVec3f& controlPoint = spline.getControlPoint(controlIndex);
        point += controlPoint * factor;
        weightSum += factor;
    }

    if (std::abs(weightSum) > 0.000001f)
        point /= weightSum;
    return point;
}

bool AddSplineGeometry(Vector2DDocument& document, uint32_t elementIndex, const dimeState* state,
    const dimeSpline& spline, const glm::vec4& color)
{
    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();

    const int fitPointCount = spline.getNumFitPoints();
    if (spline.getNumControlPoints() < 2)
    {
        if (fitPointCount < 2)
            return false;
        for (int i = 1; i < fitPointCount; ++i)
            AddLine(document, elementIndex, transform, spline.getFitPoint(i - 1), spline.getFitPoint(i), color);
        return true;
    }

    const int controlPointCount = spline.getNumControlPoints();
    int degree = std::max(1, (int)spline.getDegree());
    degree = std::min(degree, controlPointCount - 1);

    std::vector<float> knots;
    knots.reserve((size_t)spline.getNumKnots());
    for (int i = 0; i < spline.getNumKnots(); ++i)
        knots.push_back((float)spline.getKnotValue(i));

    const int requiredKnots = controlPointCount + degree + 1;
    if ((int)knots.size() < requiredKnots)
        return false;

    const float start = knots[degree];
    const float end = knots[controlPointCount];
    if (end <= start)
        return false;

    const int sampleCount = std::max(12, std::min(128, controlPointCount * degree * 8));
    dimeVec3f previous = EvaluateSpline(spline, start, knots);
    for (int i = 1; i <= sampleCount; ++i)
    {
        const float t = (float)i / (float)sampleCount;
        const float parameter = (i == sampleCount) ? end : (start + (end - start) * t);
        const dimeVec3f current = EvaluateSpline(spline, parameter, knots);
        AddLine(document, elementIndex, transform, previous, current, color);
        previous = current;
    }

    if ((spline.getFlags() & dimeSpline::CLOSED) != 0 && sampleCount > 1)
        AddLine(document, elementIndex, transform, previous, EvaluateSpline(spline, start, knots), color);

    return true;
}

int FindRoot(std::vector<int>& parents, int index)
{
    while (parents[index] != index)
    {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

void UnionElements(std::vector<int>& parents, int a, int b)
{
    const int rootA = FindRoot(parents, a);
    const int rootB = FindRoot(parents, b);
    if (rootA != rootB)
        parents[rootB] = rootA;
}

bool GetElementEndpoints(const Vector2DDocument& document, const Vector2DSubElement& element,
    glm::vec2& start, glm::vec2& end)
{
    if (element.LineIndices.empty())
        return false;

    const uint32_t firstIndex = element.LineIndices.front();
    const uint32_t lastIndex = element.LineIndices.back();
    if (firstIndex >= document.Lines.size() || lastIndex >= document.Lines.size())
        return false;

    start = document.Lines[firstIndex].Start;
    end = document.Lines[lastIndex].End;
    return true;
}

bool PointsConnected(const glm::vec2& a, const glm::vec2& b, float tolerance)
{
    return glm::length(a - b) <= tolerance;
}

void MergeConnectedElements(Vector2DDocument& document)
{
    const int elementCount = (int)document.Elements.size();
    if (elementCount <= 1)
        return;

    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    for (const Vector2DLine& line : document.Lines)
    {
        minimum = glm::min(minimum, glm::min(line.Start, line.End));
        maximum = glm::max(maximum, glm::max(line.Start, line.End));
    }
    const float diagonal = glm::length(maximum - minimum);
    const float tolerance = std::max(0.0001f, diagonal * 0.000001f);

    std::vector<glm::vec2> starts((size_t)elementCount);
    std::vector<glm::vec2> ends((size_t)elementCount);
    std::vector<bool> valid((size_t)elementCount, false);
    for (int i = 0; i < elementCount; ++i)
        valid[i] = GetElementEndpoints(document, document.Elements[(size_t)i], starts[(size_t)i], ends[(size_t)i]);

    std::vector<int> parents((size_t)elementCount);
    for (int i = 0; i < elementCount; ++i)
        parents[(size_t)i] = i;

    for (int a = 0; a < elementCount; ++a)
    {
        if (!valid[(size_t)a])
            continue;
        for (int b = a + 1; b < elementCount; ++b)
        {
            if (!valid[(size_t)b])
                continue;
            const bool connected =
                PointsConnected(starts[(size_t)a], starts[(size_t)b], tolerance) ||
                PointsConnected(starts[(size_t)a], ends[(size_t)b], tolerance) ||
                PointsConnected(ends[(size_t)a], starts[(size_t)b], tolerance) ||
                PointsConnected(ends[(size_t)a], ends[(size_t)b], tolerance);
            if (connected)
                UnionElements(parents, a, b);
        }
    }

    std::vector<Vector2DSubElement> merged;
    std::vector<int> remap((size_t)elementCount, -1);
    for (int i = 0; i < elementCount; ++i)
    {
        const int root = FindRoot(parents, i);
        int& mergedIndex = remap[(size_t)root];
        if (mergedIndex < 0)
        {
            mergedIndex = (int)merged.size();
            merged.push_back(document.Elements[(size_t)root]);
            merged.back().LineIndices.clear();
        }

        Vector2DSubElement& target = merged[(size_t)mergedIndex];
        const Vector2DSubElement& source = document.Elements[(size_t)i];
        target.LineIndices.insert(target.LineIndices.end(), source.LineIndices.begin(), source.LineIndices.end());
    }

    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)merged.size(); ++elementIndex)
    {
        for (uint32_t lineIndex : merged[(size_t)elementIndex].LineIndices)
        {
            if (lineIndex < document.Lines.size())
                document.Lines[(size_t)lineIndex].ElementIndex = elementIndex;
        }
    }

    document.Elements = std::move(merged);
}

bool DxfEntityCallback(const dimeState* const state, dimeEntity* entity, void* data)
{
    auto* context = static_cast<DxfLoadContext*>(data);
    if (!context || !context->Document || !entity)
        return true;

    const glm::vec4 color = DxfColor(entity->getColorNumber());
    const uint32_t elementIndex = BeginElement(*context->Document,
        entity->getEntityName() ? entity->getEntityName() : "DXF Entity");
    if (entity->typeId() == dimeBase::dimeSplineType)
    {
        AddSplineGeometry(*context->Document, elementIndex, state, *static_cast<dimeSpline*>(entity), color);
        if (elementIndex < context->Document->Elements.size() &&
            context->Document->Elements[elementIndex].LineIndices.empty())
            context->Document->Elements.pop_back();
        return true;
    }

    dimeArray<dimeVec3f> vertices;
    dimeArray<int> indices;
    dimeVec3f extrusionDir;
    dxfdouble thickness = 0.0f;
    const dimeEntity::GeometryType geometryType = entity->extractGeometry(vertices, indices, extrusionDir, thickness);
    if (geometryType != dimeEntity::LINES || vertices.count() < 2)
    {
        if (elementIndex < context->Document->Elements.size() &&
            context->Document->Elements[elementIndex].LineIndices.empty())
            context->Document->Elements.pop_back();
        return true;
    }

    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();
    if (thickness == 0.0f && extrusionDir != dimeVec3f(0.0f, 0.0f, 1.0f))
    {
        dimeMatrix ucs;
        dimeEntity::generateUCS(extrusionDir, ucs);
        transform.multRight(ucs);
    }

    if (indices.count() > 0)
        AddIndexedLines(*context->Document, elementIndex, transform, vertices, indices, color);
    else
        AddLineStrip(*context->Document, elementIndex, transform, vertices, color);

    if (elementIndex < context->Document->Elements.size() &&
        context->Document->Elements[elementIndex].LineIndices.empty())
        context->Document->Elements.pop_back();

    return true;
}
}

bool DxfLoader::Load(const std::filesystem::path& filepath, Vector2DDocument& document, std::string& error)
{
    document = {};
    document.SourcePath = filepath;
    try {
        document.SourceName = filepath.filename().u8string();
    } catch (...) {
        document.SourceName = filepath.filename().string();
    }

    dimeInput input;
    const std::string utf8Path = filepath.u8string();
    if (!input.setFile(utf8Path.c_str()))
    {
        error = "Failed to open DXF file.";
        return false;
    }

    dimeModel model;
    if (!model.read(&input))
    {
        error = "Failed to parse DXF file.";
        return false;
    }

    DxfLoadContext context;
    context.Document = &document;
    if (!model.traverseEntities(DxfEntityCallback, &context, false, true, false))
    {
        error = "Failed while traversing DXF entities.";
        return false;
    }
    MergeConnectedElements(document);

    if (document.Empty())
    {
        error = "DXF file contains no drawable 2D line geometry.";
        return false;
    }

    return true;
}
