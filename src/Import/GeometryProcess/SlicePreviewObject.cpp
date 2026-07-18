#include "SlicePreviewObject.h"

#include <Application.h>
#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>
#include <Renderer/VertexDesc.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
constexpr float kMinimumDisplaySegmentLength = 0.00001f;

Ref<VertexArray> BuildLineVertexArray(const std::vector<VertexColor>& vertices)
{
    Ref<VertexArray> vertexArray = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create((float*)vertices.data(), (uint32_t)(vertices.size() * sizeof(VertexColor)));
    vertexBuffer->SetLayout({
        BufferElement(ShaderDataType::Float3, "a_Position", false),
        BufferElement(ShaderDataType::Float4, "a_Color", false)
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->Unbind();
    return vertexArray;
}

void ExtendBounds(const glm::vec3& point, glm::vec3& minimum, glm::vec3& maximum)
{
    minimum = glm::min(minimum, point);
    maximum = glm::max(maximum, point);
}

bool ShouldDrawClosingSegment(const std::vector<glm::vec3>& polygon)
{
    if (polygon.size() < 3)
        return false;

    const float closingLength = glm::length(polygon.front() - polygon.back());
    if (closingLength <= kMinimumDisplaySegmentLength)
        return false;

    std::vector<float> segmentLengths;
    segmentLengths.reserve(polygon.size() - 1);
    for (size_t i = 1; i < polygon.size(); ++i)
    {
        const float length = glm::length(polygon[i] - polygon[i - 1]);
        if (length > kMinimumDisplaySegmentLength)
            segmentLengths.push_back(length);
    }

    if (segmentLengths.empty())
        return false;

    std::sort(segmentLengths.begin(), segmentLengths.end());
    const float medianLength = segmentLengths[segmentLengths.size() / 2];
    const float maxLength = segmentLengths.back();
    const float closeThreshold = std::max({ 0.001f, medianLength * 8.0f, maxLength * 3.0f });
    return closingLength <= closeThreshold;
}

void AddDisplaySegment(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color,
    std::vector<VertexColor>& vertices, glm::vec3& minimum, glm::vec3& maximum, bool& hasBounds)
{
    if (glm::length(p1 - p0) <= kMinimumDisplaySegmentLength)
        return;

    vertices.emplace_back(p0, color);
    vertices.emplace_back(p1, color);
    ExtendBounds(p0, minimum, maximum);
    ExtendBounds(p1, minimum, maximum);
    hasBounds = true;
}
}

SlicePreviewObject::~SlicePreviewObject()
{
}

bool SlicePreviewObject::LoadFromContours(const GeometryProcess::SliceContours& contours)
{
    ReleaseObjectElements();
    m_SubElementContours.clear();
    Bounds = {};

    glm::vec3 minimum((std::numeric_limits<float>::max)());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    bool hasBounds = false;

    const size_t layerCount = contours.size();
    size_t layerIndex = 0;
    for (const auto& [layer, polygons] : contours)
    {
        const float t = layerCount > 1 ? (float)layerIndex / (float)(layerCount - 1) : 0.0f;
        const glm::vec4 color = glm::mix(
            glm::vec4(0.08f, 0.92f, 1.0f, 1.0f),
            glm::vec4(1.0f, 0.78f, 0.16f, 1.0f), t);

        for (const auto& polygon : polygons)
        {
            if (polygon.size() < 2)
                continue;

            std::vector<VertexColor> vertices;
            glm::vec3 elementMinimum((std::numeric_limits<float>::max)());
            glm::vec3 elementMaximum(std::numeric_limits<float>::lowest());
            bool hasElementBounds = false;

            for (size_t i = 1; i < polygon.size(); ++i)
                AddDisplaySegment(polygon[i - 1], polygon[i], color, vertices, elementMinimum, elementMaximum, hasElementBounds);

            if (ShouldDrawClosingSegment(polygon))
                AddDisplaySegment(polygon.back(), polygon.front(), color, vertices, elementMinimum, elementMaximum, hasElementBounds);

            if (vertices.empty())
                continue;

            Object3DElement element;
            element.Name = "Layer " + std::to_string(layer + 1) + " Contour " + std::to_string(m_ObjectElements.size() + 1);
            element.Geometry = GeometryLibrary::Register(BuildLineVertexArray(vertices));
            element.VertexCount = (uint32_t)vertices.size();
            element.LineCount = element.VertexCount / 2;
            if (hasElementBounds)
            {
                element.Bounds.Center = (elementMinimum + elementMaximum) * 0.5f;
                element.Bounds.Radius = 0.0f;
                for (const VertexColor& vertex : vertices)
                    element.Bounds.Radius = std::max(element.Bounds.Radius, glm::length(vertex.Position - element.Bounds.Center));
                element.Bounds.Valid = true;
            }
            m_ObjectElements.push_back(std::move(element));
            m_SubElementContours.push_back(polygon);

            if (hasElementBounds)
            {
                minimum = glm::min(minimum, elementMinimum);
                maximum = glm::max(maximum, elementMaximum);
                hasBounds = true;
            }
        }
        ++layerIndex;
    }

    if (m_ObjectElements.empty())
        return false;

    if (hasBounds)
    {
        Bounds.Center = (minimum + maximum) * 0.5f;
        Bounds.Radius = 0.0f;
        for (const Object3DElement& element : m_ObjectElements)
        {
            if (!element.Bounds.Valid)
                continue;
            Bounds.Radius = std::max(Bounds.Radius,
                glm::length(element.Bounds.Center - Bounds.Center) + element.Bounds.Radius);
        }
        Bounds.Valid = true;
    }

    return true;
}

void SlicePreviewObject::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass || GetVisibleVertexCount() == 0)
        return;

    auto shader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
    shader->Bind();
    shader->SetMat4("u_View", view);
    shader->SetMat4("u_Projection", proj);
    shader->SetMat4("u_Model", Transfm.GetMatrix());
    shader->SetFloat("u_ObjectOpacity", 1.0f);
    shader->SetInt("u_TransparentPass", 0);

    RenderCommand::SetLineWidth(2.0f);
    for (const Object3DElement& element : m_ObjectElements)
    {
        if (!element.Visible || element.VertexCount == 0)
            continue;

        Ref<VertexArray> lineGeometry = GeometryLibrary::Resolve(element.Geometry);
        if (lineGeometry)
            RenderCommand::DrawLines(lineGeometry, element.VertexCount);
    }
}

void SlicePreviewObject::DrawPickup(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, int objectID, bool xzInput, float xzInputY)
{
    if (!shader || GetVisibleVertexCount() == 0)
        return;

    shader->SetMat4("u_View", view);
    shader->SetMat4("u_Projection", proj);
    shader->SetMat4("u_Model", Transfm.GetMatrix());
    shader->SetInt("u_ObjectID", objectID);
    shader->SetInt("u_XZInput", xzInput ? 1 : 0);
    shader->SetFloat("u_XZInputY", xzInputY);

    RenderCommand::SetLineWidth(8.0f);
    for (const Object3DElement& element : m_ObjectElements)
    {
        if (!element.Visible || element.VertexCount == 0)
            continue;

        Ref<VertexArray> lineGeometry = GeometryLibrary::Resolve(element.Geometry);
        if (lineGeometry)
            RenderCommand::DrawLines(lineGeometry, element.VertexCount);
    }
    RenderCommand::SetLineWidth(1.0f);
}

void SlicePreviewObject::DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, bool xzInput, float xzInputY)
{
    if (!shader || GetVisibleVertexCount() == 0)
        return;

    shader->SetMat4("u_View", view);
    shader->SetMat4("u_Projection", proj);
    shader->SetMat4("u_Model", Transfm.GetMatrix());
    shader->SetInt("u_XZInput", xzInput ? 1 : 0);
    shader->SetFloat("u_XZInputY", xzInputY);

    RenderCommand::SetLineWidth(4.0f);
    for (const Object3DElement& element : m_ObjectElements)
    {
        if (!element.Visible || element.VertexCount == 0)
            continue;

        Ref<VertexArray> lineGeometry = GeometryLibrary::Resolve(element.Geometry);
        if (lineGeometry)
            RenderCommand::DrawLines(lineGeometry, element.VertexCount);
    }
    RenderCommand::SetLineWidth(1.0f);
}

void SlicePreviewObject::UpdateBoundingSphere()
{
}

const std::vector<glm::vec3>* SlicePreviewObject::GetSubElementContour(size_t index) const
{
    return index < m_SubElementContours.size() ? &m_SubElementContours[index] : nullptr;
}

uint32_t SlicePreviewObject::GetVisibleVertexCount() const
{
    uint32_t vertexCount = 0;
    for (const Object3DElement& element : m_ObjectElements)
    {
        if (element.Visible)
            vertexCount += element.VertexCount;
    }
    return vertexCount;
}
