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
    GeometryLibrary::Release(m_LineGeometry);
}

bool SlicePreviewObject::LoadFromContours(const GeometryProcess::SliceContours& contours)
{
    GeometryLibrary::Release(m_LineGeometry);
    m_LineGeometry = {};
    m_LineVertexCount = 0;
    Bounds = {};

    std::vector<VertexColor> vertices;
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

            for (size_t i = 1; i < polygon.size(); ++i)
                AddDisplaySegment(polygon[i - 1], polygon[i], color, vertices, minimum, maximum, hasBounds);

            if (ShouldDrawClosingSegment(polygon))
                AddDisplaySegment(polygon.back(), polygon.front(), color, vertices, minimum, maximum, hasBounds);
        }
        ++layerIndex;
    }

    if (vertices.empty())
        return false;

    m_LineGeometry = GeometryLibrary::Register(BuildLineVertexArray(vertices));
    m_LineVertexCount = (uint32_t)vertices.size();

    if (hasBounds)
    {
        Bounds.Center = (minimum + maximum) * 0.5f;
        Bounds.Radius = 0.0f;
        for (const VertexColor& vertex : vertices)
            Bounds.Radius = std::max(Bounds.Radius, glm::length(vertex.Position - Bounds.Center));
        Bounds.Valid = true;
    }

    return true;
}

void SlicePreviewObject::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass || m_LineVertexCount == 0)
        return;

    Ref<VertexArray> lineGeometry = GeometryLibrary::Resolve(m_LineGeometry);
    if (!lineGeometry)
        return;

    auto shader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
    shader->Bind();
    shader->SetMat4("u_View", view);
    shader->SetMat4("u_Projection", proj);
    shader->SetMat4("u_Model", Transfm.GetMatrix());
    shader->SetFloat("u_ObjectOpacity", 1.0f);
    shader->SetInt("u_TransparentPass", 0);

    RenderCommand::SetLineWidth(2.0f);
    RenderCommand::DrawLines(lineGeometry, m_LineVertexCount);
}

void SlicePreviewObject::DrawPickup(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, int objectID, bool xzInput, float xzInputY)
{
    if (!shader || m_LineVertexCount == 0)
        return;

    Ref<VertexArray> lineGeometry = GeometryLibrary::Resolve(m_LineGeometry);
    if (!lineGeometry)
        return;

    shader->SetMat4("u_View", view);
    shader->SetMat4("u_Projection", proj);
    shader->SetMat4("u_Model", Transfm.GetMatrix());
    shader->SetInt("u_ObjectID", objectID);
    shader->SetInt("u_XZInput", xzInput ? 1 : 0);
    shader->SetFloat("u_XZInputY", xzInputY);

    RenderCommand::SetLineWidth(8.0f);
    RenderCommand::DrawLines(lineGeometry, m_LineVertexCount);
    RenderCommand::SetLineWidth(1.0f);
}

void SlicePreviewObject::DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, bool xzInput, float xzInputY)
{
    if (!shader || m_LineVertexCount == 0)
        return;

    Ref<VertexArray> lineGeometry = GeometryLibrary::Resolve(m_LineGeometry);
    if (!lineGeometry)
        return;

    shader->SetMat4("u_View", view);
    shader->SetMat4("u_Projection", proj);
    shader->SetMat4("u_Model", Transfm.GetMatrix());
    shader->SetInt("u_XZInput", xzInput ? 1 : 0);
    shader->SetFloat("u_XZInputY", xzInputY);

    RenderCommand::SetLineWidth(4.0f);
    RenderCommand::DrawLines(lineGeometry, m_LineVertexCount);
    RenderCommand::SetLineWidth(1.0f);
}

void SlicePreviewObject::UpdateBoundingSphere()
{
}
