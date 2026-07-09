#include "stdsfx.h"
#include "DxfLoader.h"

#include <Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

#include <dime/Base.h>
#include <dime/Input.h>
#include <dime/Model.h>
#include <dime/State.h>
#include <dime/entities/Arc.h>
#include <dime/entities/Circle.h>
#include <dime/entities/Entity.h>
#include <dime/entities/LWPolyline.h>
#include <dime/entities/Polyline.h>
#include <dime/entities/Spline.h>
#include <dime/entities/Vertex.h>
#include <dime/util/Array.h>
#include <dime/util/Linear.h>

namespace
{
struct DxfLoadContext
{
    Vector2DDocument* Document = nullptr;
    DxfImportMode Mode = DxfImportMode::LinesWithArcFit;
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

uint32_t AddPrimitiveLine(Vector2DDocument& document, uint32_t elementIndex, const Vector2DLine& line);
uint32_t AddPrimitiveArc(Vector2DDocument& document, uint32_t elementIndex, const Vector2DArc& arc);

dimeVec3f TransformPoint(const dimeMatrix& matrix, const dimeVec3f& point)
{
    dimeVec3f transformed = point;
    matrix.multMatrixVec(transformed);
    return transformed;
}

void AddLine(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeVec3f& start, const dimeVec3f& end, const glm::vec4& color, bool emitPrimitive = false,
    bool displayAsArc = false)
{
    const dimeVec3f transformedStart = TransformPoint(transform, start);
    const dimeVec3f transformedEnd = TransformPoint(transform, end);
    if (std::abs(transformedStart.x - transformedEnd.x) < 0.000001f &&
        std::abs(transformedStart.y - transformedEnd.y) < 0.000001f)
        return;

    const uint32_t lineIndex = (uint32_t)document.Lines.size();
    Vector2DLine line;
    line.Start = ToVec2(transformedStart);
    line.End = ToVec2(transformedEnd);
    line.Color = color;
    line.ElementIndex = elementIndex;
    line.DisplayAsArc = displayAsArc;
    document.Lines.push_back(line);
    if (elementIndex < document.Elements.size())
        document.Elements[elementIndex].LineIndices.push_back(lineIndex);
    if (emitPrimitive)
        AddPrimitiveLine(document, elementIndex, document.Lines.back());
}

void AddLineStrip(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeArray<dimeVec3f>& vertices, const glm::vec4& color, bool emitPrimitives = false,
    bool displayAsArc = false)
{
    for (int i = 1; i < vertices.count(); ++i)
        AddLine(document, elementIndex, transform, vertices[i - 1], vertices[i], color, emitPrimitives,
            displayAsArc);
}

void AddIndexedLines(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeArray<dimeVec3f>& vertices, const dimeArray<int>& indices, const glm::vec4& color,
    bool emitPrimitives = false)
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
            AddLine(document, elementIndex, transform, vertices[previous], vertices[current], color, emitPrimitives);
        previous = current;
    }
}

double GetEntityDoubleRecord(const dimeEntity& entity, int groupCode, double fallback = 0.0)
{
    dimeParam param;
    return entity.getRecord(groupCode, param) ? param.double_data : fallback;
}

void AddBulgeSegment(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeVec3f& start, const dimeVec3f& end, double bulge, const glm::vec4& color, bool emitNativePrimitive = false)
{
    if (std::abs(bulge) < 0.000001)
    {
        AddLine(document, elementIndex, transform, start, end, color, emitNativePrimitive);
        return;
    }

    const glm::dvec2 p0(start.x, start.y);
    const glm::dvec2 p1(end.x, end.y);
    const glm::dvec2 chord = p1 - p0;
    const double chordLength = glm::length(chord);
    if (chordLength < 0.000001)
        return;

    const double includedAngle = 4.0 * std::atan(bulge);
    const double halfAngle = includedAngle * 0.5;
    const double sinHalfAngle = std::sin(halfAngle);
    if (std::abs(sinHalfAngle) < 0.000001)
    {
        AddLine(document, elementIndex, transform, start, end, color);
        return;
    }

    const glm::dvec2 direction = chord / chordLength;
    const glm::dvec2 normal(-direction.y, direction.x);
    const glm::dvec2 midpoint = (p0 + p1) * 0.5;
    const double centerOffset = chordLength * (1.0 - bulge * bulge) / (4.0 * bulge);
    const glm::dvec2 center = midpoint + normal * centerOffset;
    const double radius = glm::length(p0 - center);
    if (radius < 0.000001)
    {
        AddLine(document, elementIndex, transform, start, end, color);
        return;
    }

    const double startAngle = std::atan2(p0.y - center.y, p0.x - center.x);
    if (emitNativePrimitive)
    {
        const dimeVec3f transformedStart = TransformPoint(transform, start);
        const dimeVec3f transformedEnd = TransformPoint(transform, end);
        const dimeVec3f transformedCenter = TransformPoint(transform,
            dimeVec3f((dxfdouble)center.x, (dxfdouble)center.y, start.z));
        const glm::vec2 arcStart = ToVec2(transformedStart);
        const glm::vec2 arcEnd = ToVec2(transformedEnd);
        const glm::vec2 arcCenter = ToVec2(transformedCenter);

        Vector2DArc arc;
        arc.Start = arcStart;
        arc.End = arcEnd;
        arc.Center = arcCenter;
        arc.Radius = glm::length(arcStart - arcCenter);
        arc.StartAngle = std::atan2(arcStart.y - arcCenter.y, arcStart.x - arcCenter.x);
        arc.SweepAngle = (float)includedAngle;
        arc.Color = color;
        arc.ElementIndex = elementIndex;
        if (arc.Radius > 0.000001f)
            AddPrimitiveArc(document, elementIndex, arc);
    }

    const int sampleCount = std::max(4, std::min(96, (int)std::ceil(std::abs(includedAngle) / 0.08726646259971647)));
    dimeVec3f previous = start;
    for (int i = 1; i <= sampleCount; ++i)
    {
        const double t = (double)i / (double)sampleCount;
        const double angle = startAngle + includedAngle * t;
        dimeVec3f current(
            (dxfdouble)(center.x + std::cos(angle) * radius),
            (dxfdouble)(center.y + std::sin(angle) * radius),
            start.z + (end.z - start.z) * (dxfdouble)t);
        if (i == sampleCount)
            current = end;
        AddLine(document, elementIndex, transform, previous, current, color, false, emitNativePrimitive);
        previous = current;
    }
}

bool AddPolylineGeometry(Vector2DDocument& document, uint32_t elementIndex, const dimeState* state,
    dimePolyline& polyline, const glm::vec4& color, DxfImportMode mode)
{
    if (polyline.getType() != dimePolyline::POLYLINE || polyline.getNumCoordVertices() < 2)
        return false;

    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();
    const dimeVec3f extrusionDir = polyline.getExtrusionDir();
    const dxfdouble thickness = polyline.getThickness();
    if (thickness == 0.0 && extrusionDir != dimeVec3f(0.0f, 0.0f, 1.0f))
    {
        dimeMatrix ucs;
        dimeEntity::generateUCS(extrusionDir, ucs);
        transform.multRight(ucs);
    }

    const int vertexCount = polyline.getNumCoordVertices();
    const bool closed = (polyline.getFlags() & dimePolyline::CLOSED) != 0;
    const int segmentCount = closed ? vertexCount : vertexCount - 1;
    for (int i = 0; i < segmentCount; ++i)
    {
        const int nextIndex = (i + 1) % vertexCount;
        const dimeVertex* startVertex = polyline.getCoordVertex(i);
        const dimeVertex* endVertex = polyline.getCoordVertex(nextIndex);
        if (!startVertex || !endVertex)
            continue;

        const double bulge = GetEntityDoubleRecord(*startVertex, 42, 0.0);
        AddBulgeSegment(document, elementIndex, transform,
            startVertex->getCoords(), endVertex->getCoords(), bulge, color,
            mode == DxfImportMode::NativePrimitives);
    }

    return elementIndex < document.Elements.size() && !document.Elements[elementIndex].LineIndices.empty();
}

bool AddLWPolylineGeometry(Vector2DDocument& document, uint32_t elementIndex, const dimeState* state,
    dimeLWPolyline& polyline, const glm::vec4& color, DxfImportMode mode)
{
    const int vertexCount = polyline.getNumVertices();
    if (vertexCount < 2)
        return false;

    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();
    const dimeVec3f extrusionDir = polyline.getExtrusionDir();
    const dxfdouble thickness = polyline.getThickness();
    if (thickness == 0.0 && extrusionDir != dimeVec3f(0.0f, 0.0f, 1.0f))
    {
        dimeMatrix ucs;
        dimeEntity::generateUCS(extrusionDir, ucs);
        transform.multRight(ucs);
    }

    const dxfdouble* xs = polyline.getXCoords();
    const dxfdouble* ys = polyline.getYCoords();
    const dxfdouble* bulges = polyline.getBulges();
    if (!xs || !ys)
        return false;

    const bool closed = (polyline.getFlags() & 1) != 0;
    const int segmentCount = closed ? vertexCount : vertexCount - 1;
    for (int i = 0; i < segmentCount; ++i)
    {
        const int nextIndex = (i + 1) % vertexCount;
        const dimeVec3f start(xs[i], ys[i], polyline.getElevation());
        const dimeVec3f end(xs[nextIndex], ys[nextIndex], polyline.getElevation());
        const double bulge = bulges ? bulges[i] : 0.0;
        AddBulgeSegment(document, elementIndex, transform, start, end, bulge, color,
            mode == DxfImportMode::NativePrimitives);
    }

    return elementIndex < document.Elements.size() && !document.Elements[elementIndex].LineIndices.empty();
}

void AddNativeArcPrimitive(Vector2DDocument& document, uint32_t elementIndex, const dimeMatrix& transform,
    const dimeVec3f& center, double radius, double startAngleRadians, double sweepAngleRadians,
    const glm::vec4& color)
{
    const dimeVec3f localStart(
        (dxfdouble)(center.x + std::cos(startAngleRadians) * radius),
        (dxfdouble)(center.y + std::sin(startAngleRadians) * radius),
        center.z);
    const dimeVec3f localEnd(
        (dxfdouble)(center.x + std::cos(startAngleRadians + sweepAngleRadians) * radius),
        (dxfdouble)(center.y + std::sin(startAngleRadians + sweepAngleRadians) * radius),
        center.z);

    const glm::vec2 arcStart = ToVec2(TransformPoint(transform, localStart));
    const glm::vec2 arcEnd = ToVec2(TransformPoint(transform, localEnd));
    const glm::vec2 arcCenter = ToVec2(TransformPoint(transform, center));

    Vector2DArc arc;
    arc.Start = arcStart;
    arc.End = arcEnd;
    arc.Center = arcCenter;
    arc.Radius = glm::length(arcStart - arcCenter);
    arc.StartAngle = std::atan2(arcStart.y - arcCenter.y, arcStart.x - arcCenter.x);
    arc.SweepAngle = (float)sweepAngleRadians;
    arc.Color = color;
    arc.ElementIndex = elementIndex;
    if (arc.Radius > 0.000001f)
        AddPrimitiveArc(document, elementIndex, arc);
}

bool AddArcEntityGeometry(Vector2DDocument& document, uint32_t elementIndex, const dimeState* state,
    dimeArc& arcEntity, const glm::vec4& color, DxfImportMode mode)
{
    dimeArray<dimeVec3f> vertices;
    dimeArray<int> indices;
    dimeVec3f extrusionDir;
    dxfdouble thickness = 0.0f;
    if (arcEntity.extractGeometry(vertices, indices, extrusionDir, thickness) != dimeEntity::LINES ||
        vertices.count() < 2)
        return false;

    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();
    if (thickness == 0.0f && extrusionDir != dimeVec3f(0.0f, 0.0f, 1.0f))
    {
        dimeMatrix ucs;
        dimeEntity::generateUCS(extrusionDir, ucs);
        transform.multRight(ucs);
    }

    AddLineStrip(document, elementIndex, transform, vertices, color, false,
        mode == DxfImportMode::NativePrimitives);

    if (mode == DxfImportMode::NativePrimitives)
    {
        dimeVec3f center;
        arcEntity.getCenter(center);
        double endAngle = arcEntity.getEndAngle();
        while (endAngle < arcEntity.getStartAngle())
            endAngle += 360.0;
        const double startRadians = arcEntity.getStartAngle() * 0.017453292519943295;
        double sweepRadians = (endAngle - arcEntity.getStartAngle()) * 0.017453292519943295;
        if (std::abs(sweepRadians) < 0.000001)
            sweepRadians = 6.28318530717958647692;
        AddNativeArcPrimitive(document, elementIndex, transform, center, arcEntity.getRadius(),
            startRadians, sweepRadians, color);
    }

    return elementIndex < document.Elements.size() && !document.Elements[elementIndex].LineIndices.empty();
}

bool AddCircleEntityGeometry(Vector2DDocument& document, uint32_t elementIndex, const dimeState* state,
    dimeCircle& circleEntity, const glm::vec4& color, DxfImportMode mode)
{
    dimeArray<dimeVec3f> vertices;
    dimeArray<int> indices;
    dimeVec3f extrusionDir;
    dxfdouble thickness = 0.0f;
    if (circleEntity.extractGeometry(vertices, indices, extrusionDir, thickness) != dimeEntity::LINES ||
        vertices.count() < 2)
        return false;

    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();
    if (thickness == 0.0f && extrusionDir != dimeVec3f(0.0f, 0.0f, 1.0f))
    {
        dimeMatrix ucs;
        dimeEntity::generateUCS(extrusionDir, ucs);
        transform.multRight(ucs);
    }

    AddLineStrip(document, elementIndex, transform, vertices, color, false,
        mode == DxfImportMode::NativePrimitives);

    if (mode == DxfImportMode::NativePrimitives)
    {
        const dimeVec3f center = circleEntity.getCenter();
        AddNativeArcPrimitive(document, elementIndex, transform, center, circleEntity.getRadius(),
            0.0, 3.14159265358979323846, color);
        AddNativeArcPrimitive(document, elementIndex, transform, center, circleEntity.getRadius(),
            3.14159265358979323846, 3.14159265358979323846, color);
    }

    return elementIndex < document.Elements.size() && !document.Elements[elementIndex].LineIndices.empty();
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
    const dimeSpline& spline, const glm::vec4& color, DxfImportMode mode)
{
    dimeMatrix transform = state ? state->getMatrix() : dimeMatrix::identity();
    const bool emitLinePrimitives = mode == DxfImportMode::NativePrimitives;

    const int fitPointCount = spline.getNumFitPoints();
    if (spline.getNumControlPoints() < 2)
    {
        if (fitPointCount < 2)
            return false;
        for (int i = 1; i < fitPointCount; ++i)
            AddLine(document, elementIndex, transform, spline.getFitPoint(i - 1), spline.getFitPoint(i), color,
                emitLinePrimitives);
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
        AddLine(document, elementIndex, transform, previous, current, color, emitLinePrimitives);
        previous = current;
    }

    if ((spline.getFlags() & dimeSpline::CLOSED) != 0 && sampleCount > 1)
        AddLine(document, elementIndex, transform, previous, EvaluateSpline(spline, start, knots), color,
            emitLinePrimitives);

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

bool ColorsEqual(const glm::vec4& a, const glm::vec4& b)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec4(0.000001f)));
}

float NormalizeAnglePositive(float angle)
{
    constexpr float twoPi = 6.28318530717958647692f;
    while (angle < 0.0f)
        angle += twoPi;
    while (angle >= twoPi)
        angle -= twoPi;
    return angle;
}

float AngleDistanceCCW(float start, float end)
{
    return NormalizeAnglePositive(end - start);
}

float SignedTurnAngle(const glm::vec2& previous, const glm::vec2& next)
{
    const float cross = previous.x * next.y - previous.y * next.x;
    const float dot = glm::dot(previous, next);
    return std::atan2(cross, dot);
}

float PrefixRangeSum(const std::vector<float>& prefix, size_t startIndex, size_t endIndex)
{
    if (endIndex <= startIndex + 1 || endIndex >= prefix.size())
        return 0.0f;
    return prefix[endIndex] - prefix[startIndex + 1];
}

bool CircleFromThreePoints(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c,
    glm::vec2& center, float& radius)
{
    const float d = 2.0f * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 0.000001f)
        return false;

    const float aa = glm::dot(a, a);
    const float bb = glm::dot(b, b);
    const float cc = glm::dot(c, c);
    center.x = (aa * (b.y - c.y) + bb * (c.y - a.y) + cc * (a.y - b.y)) / d;
    center.y = (aa * (c.x - b.x) + bb * (a.x - c.x) + cc * (b.x - a.x)) / d;
    radius = glm::length(a - center);
    return radius > 0.000001f;
}

bool FitArcToPoints(const std::vector<glm::vec2>& points, size_t startIndex, size_t endIndex,
    float tolerance, float turnSum, Vector2DArc& arc)
{
    constexpr float pi = 3.14159265358979323846f;
    if (endIndex <= startIndex + 2)
        return false;

    const glm::vec2& start = points[startIndex];
    const glm::vec2& middle = points[(startIndex + endIndex) / 2];
    const glm::vec2& end = points[endIndex];

    glm::vec2 center(0.0f);
    float radius = 0.0f;
    if (!CircleFromThreePoints(start, middle, end, center, radius))
        return false;

    const float chordLength = glm::length(end - start);
    if (chordLength < tolerance)
        return false;

    if (std::abs(turnSum) < tolerance * tolerance)
        return false;

    const bool ccw = turnSum > 0.0f;
    const float startAngle = std::atan2(start.y - center.y, start.x - center.x);
    const float endAngle = std::atan2(end.y - center.y, end.x - center.x);
    const float sweep = ccw ? AngleDistanceCCW(startAngle, endAngle) : -AngleDistanceCCW(endAngle, startAngle);
    const float absSweep = std::abs(sweep);
    if (absSweep < 0.0523598776f || absSweep > pi + 0.001f)
        return false;

    const float sagitta = radius * (1.0f - std::cos(absSweep * 0.5f));
    if (sagitta / std::max(chordLength, 0.000001f) < 0.0001f)
        return false;

    float previousProgress = 0.0f;
    for (size_t i = startIndex; i <= endIndex; ++i)
    {
        const glm::vec2 offset = points[i] - center;
        const float distance = glm::length(offset);
        if (std::abs(distance - radius) > tolerance)
            return false;

        const float angle = std::atan2(offset.y, offset.x);
        const float progress = ccw ? AngleDistanceCCW(startAngle, angle) : AngleDistanceCCW(angle, startAngle);
        if (progress > absSweep + 0.001f)
            return false;
        if (i > startIndex && progress + 0.001f < previousProgress)
            return false;
        previousProgress = progress;
    }

    arc.Start = start;
    arc.End = end;
    arc.Center = center;
    arc.Radius = radius;
    arc.StartAngle = startAngle;
    arc.SweepAngle = sweep;
    return true;
}

uint32_t AddPrimitiveLine(Vector2DDocument& document, uint32_t elementIndex, const Vector2DLine& line)
{
    Vector2DPrimitive primitive;
    primitive.Type = Vector2DPrimitiveType::Line;
    primitive.Line = line;
    primitive.Line.ElementIndex = elementIndex;

    const uint32_t primitiveIndex = (uint32_t)document.Primitives.size();
    document.Primitives.push_back(primitive);
    if (elementIndex < document.Elements.size())
        document.Elements[elementIndex].PrimitiveIndices.push_back(primitiveIndex);
    return primitiveIndex;
}

uint32_t AddPrimitiveArc(Vector2DDocument& document, uint32_t elementIndex, const Vector2DArc& arc)
{
    Vector2DPrimitive primitive;
    primitive.Type = Vector2DPrimitiveType::Arc;
    primitive.Arc = arc;
    primitive.Arc.ElementIndex = elementIndex;

    const uint32_t primitiveIndex = (uint32_t)document.Primitives.size();
    document.Primitives.push_back(primitive);
    if (elementIndex < document.Elements.size())
        document.Elements[elementIndex].PrimitiveIndices.push_back(primitiveIndex);
    return primitiveIndex;
}

void FitLineRunToPrimitives(Vector2DDocument& document, uint32_t elementIndex,
    const std::vector<uint32_t>& lineIndices, float tolerance)
{
    if (lineIndices.empty())
        return;

    constexpr size_t minArcSegmentCount = 3;
    if (lineIndices.size() < minArcSegmentCount)
    {
        for (uint32_t lineIndex : lineIndices)
            AddPrimitiveLine(document, elementIndex, document.Lines[lineIndex]);
        return;
    }

    std::vector<glm::vec2> points;
    points.reserve(lineIndices.size() + 1);
    points.push_back(document.Lines[lineIndices.front()].Start);
    for (uint32_t lineIndex : lineIndices)
        points.push_back(document.Lines[lineIndex].End);

    size_t i = 0;
    while (i + 1 < points.size())
    {
        Vector2DArc bestArc;
        size_t bestEnd = i;
        for (size_t candidateEnd = i + minArcSegmentCount; candidateEnd < points.size(); ++candidateEnd)
        {
            Vector2DArc candidate;
            if (!FitArcToPoints(points, i, candidateEnd, tolerance, candidate))
                continue;

            bestArc = candidate;
            bestEnd = candidateEnd;
        }

        if (bestEnd > i)
        {
            bestArc.Color = document.Lines[lineIndices[i]].Color;
            AddPrimitiveArc(document, elementIndex, bestArc);
            for (size_t lineOffset = i; lineOffset < bestEnd; ++lineOffset)
            {
                const uint32_t lineIndex = lineIndices[lineOffset];
                if (lineIndex < document.Lines.size())
                    document.Lines[lineIndex].DisplayAsArc = true;
            }
            i = bestEnd;
        }
        else
        {
            AddPrimitiveLine(document, elementIndex, document.Lines[lineIndices[i]]);
            ++i;
        }
    }
}

void BuildFittedPrimitives(Vector2DDocument& document)
{
    document.Primitives.clear();
    for (Vector2DLine& line : document.Lines)
        line.DisplayAsArc = false;
    for (Vector2DSubElement& element : document.Elements)
        element.PrimitiveIndices.clear();

    if (document.Lines.empty())
        return;

    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    for (const Vector2DLine& line : document.Lines)
    {
        minimum = glm::min(minimum, glm::min(line.Start, line.End));
        maximum = glm::max(maximum, glm::max(line.Start, line.End));
    }
    const float diagonal = glm::length(maximum - minimum);
    const float tolerance = std::max(0.01f, diagonal * 0.00001f);
    const float connectTolerance = std::max(0.0001f, diagonal * 0.000001f);

    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)document.Elements.size(); ++elementIndex)
    {
        const Vector2DSubElement& element = document.Elements[elementIndex];
        std::vector<uint32_t> run;
        run.reserve(element.LineIndices.size());

        for (uint32_t lineIndex : element.LineIndices)
        {
            if (lineIndex >= document.Lines.size())
                continue;

            const Vector2DLine& line = document.Lines[lineIndex];
            if (!run.empty())
            {
                const Vector2DLine& previous = document.Lines[run.back()];
                const bool continuous = PointsConnected(previous.End, line.Start, connectTolerance);
                const bool sameColor = ColorsEqual(previous.Color, line.Color);
                if (!continuous || !sameColor)
                {
                    FitLineRunToPrimitives(document, elementIndex, run, tolerance);
                    run.clear();
                }
            }
            run.push_back(lineIndex);
        }

        FitLineRunToPrimitives(document, elementIndex, run, tolerance);
    }
}

void BuildLinePrimitives(Vector2DDocument& document)
{
    document.Primitives.clear();
    for (Vector2DLine& line : document.Lines)
        line.DisplayAsArc = false;
    for (Vector2DSubElement& element : document.Elements)
        element.PrimitiveIndices.clear();

    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)document.Elements.size(); ++elementIndex)
    {
        Vector2DSubElement& element = document.Elements[(size_t)elementIndex];
        for (uint32_t lineIndex : element.LineIndices)
        {
            if (lineIndex < document.Lines.size())
                AddPrimitiveLine(document, elementIndex, document.Lines[(size_t)lineIndex]);
        }
    }
}

void LogArcFitSummary(const Vector2DDocument& document)
{
    size_t fittedLineCount = 0;
    size_t fittedArcCount = 0;
    for (const Vector2DPrimitive& primitive : document.Primitives)
    {
        if (primitive.Type == Vector2DPrimitiveType::Arc)
            ++fittedArcCount;
        else
            ++fittedLineCount;
    }

    const size_t originalLineCount = document.Lines.size();
    const size_t fittedPrimitiveCount = document.Primitives.size();
    const size_t savedCount = originalLineCount > fittedPrimitiveCount ?
        originalLineCount - fittedPrimitiveCount : 0;
    const double reduction = originalLineCount > 0 ?
        (double)savedCount * 100.0 / (double)originalLineCount : 0.0;

    INFO("DXF line-to-arc fit: file='{}', original_lines={}, fitted_primitives={}, fitted_lines={}, fitted_arcs={}, reduced={} ({:.2f}%)",
        document.SourceName, originalLineCount, fittedPrimitiveCount, fittedLineCount, fittedArcCount,
        savedCount, reduction);
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
            merged.back().PrimitiveIndices.clear();
        }

        Vector2DSubElement& target = merged[(size_t)mergedIndex];
        const Vector2DSubElement& source = document.Elements[(size_t)i];
        target.LineIndices.insert(target.LineIndices.end(), source.LineIndices.begin(), source.LineIndices.end());
        target.PrimitiveIndices.insert(target.PrimitiveIndices.end(),
            source.PrimitiveIndices.begin(), source.PrimitiveIndices.end());
    }

    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)merged.size(); ++elementIndex)
    {
        for (uint32_t lineIndex : merged[(size_t)elementIndex].LineIndices)
        {
            if (lineIndex < document.Lines.size())
                document.Lines[(size_t)lineIndex].ElementIndex = elementIndex;
        }
        for (uint32_t primitiveIndex : merged[(size_t)elementIndex].PrimitiveIndices)
        {
            if (primitiveIndex >= document.Primitives.size())
                continue;
            Vector2DPrimitive& primitive = document.Primitives[(size_t)primitiveIndex];
            if (primitive.Type == Vector2DPrimitiveType::Arc)
                primitive.Arc.ElementIndex = elementIndex;
            else
                primitive.Line.ElementIndex = elementIndex;
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
    if (entity->typeId() == dimeBase::dimeArcType)
    {
        AddArcEntityGeometry(*context->Document, elementIndex, state, *static_cast<dimeArc*>(entity),
            color, context->Mode);
        if (elementIndex < context->Document->Elements.size() &&
            context->Document->Elements[elementIndex].LineIndices.empty())
            context->Document->Elements.pop_back();
        return true;
    }
    if (entity->typeId() == dimeBase::dimeCircleType)
    {
        AddCircleEntityGeometry(*context->Document, elementIndex, state, *static_cast<dimeCircle*>(entity),
            color, context->Mode);
        if (elementIndex < context->Document->Elements.size() &&
            context->Document->Elements[elementIndex].LineIndices.empty())
            context->Document->Elements.pop_back();
        return true;
    }
    if (entity->typeId() == dimeBase::dimeLWPolylineType)
    {
        AddLWPolylineGeometry(*context->Document, elementIndex, state, *static_cast<dimeLWPolyline*>(entity),
            color, context->Mode);
        if (elementIndex < context->Document->Elements.size() &&
            context->Document->Elements[elementIndex].LineIndices.empty())
            context->Document->Elements.pop_back();
        return true;
    }
    if (entity->typeId() == dimeBase::dimeSplineType)
    {
        AddSplineGeometry(*context->Document, elementIndex, state, *static_cast<dimeSpline*>(entity),
            color, context->Mode);
        if (elementIndex < context->Document->Elements.size() &&
            context->Document->Elements[elementIndex].LineIndices.empty())
            context->Document->Elements.pop_back();
        return true;
    }
    if (entity->typeId() == dimeBase::dimePolylineType)
    {
        AddPolylineGeometry(*context->Document, elementIndex, state, *static_cast<dimePolyline*>(entity),
            color, context->Mode);
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

    const bool emitLinePrimitives = context->Mode == DxfImportMode::NativePrimitives;
    if (indices.count() > 0)
        AddIndexedLines(*context->Document, elementIndex, transform, vertices, indices, color, emitLinePrimitives);
    else
        AddLineStrip(*context->Document, elementIndex, transform, vertices, color, emitLinePrimitives);

    if (elementIndex < context->Document->Elements.size() &&
        context->Document->Elements[elementIndex].LineIndices.empty())
        context->Document->Elements.pop_back();

    return true;
}

bool OpenDxfInput(const std::filesystem::path& filepath, dimeInput& input)
{
#ifdef _WIN32
    int fd = -1;
    if (_wsopen_s(&fd, filepath.native().c_str(), _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD) != 0)
        return false;
    return input.setFilePointer(fd);
#else
    return input.setFile(filepath.c_str());
#endif
}
}

bool DxfLoader::Load(const std::filesystem::path& filepath, Vector2DDocument& document, std::string& error,
    DxfImportMode mode)
{
    document = {};
    document.SourcePath = filepath;
    try {
        document.SourceName = filepath.filename().u8string();
    } catch (...) {
        document.SourceName = filepath.filename().string();
    }

    dimeInput input;
    if (!OpenDxfInput(filepath, input))
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
    context.Mode = mode;
    if (!model.traverseEntities(DxfEntityCallback, &context, false, true, false))
    {
        error = "Failed while traversing DXF entities.";
        return false;
    }
    MergeConnectedElements(document);
    if (mode == DxfImportMode::LinesOnly)
        BuildLinePrimitives(document);
    else if (mode == DxfImportMode::LinesWithArcFit)
    {
        BuildFittedPrimitives(document);
        LogArcFitSummary(document);
    }

    if (document.Empty())
    {
        error = "DXF file contains no drawable 2D line geometry.";
        return false;
    }

    return true;
}
