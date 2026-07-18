#include "stdsfx.h"
#include "Object2D.h"

#include <Application.h>
#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>

#include <cmath>
#include <limits>

namespace
{
uint32_t HashString(const std::string& value)
{
    uint32_t hash = 2166136261u;
    for (unsigned char c : value)
    {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

glm::vec4 DisplayColorForFile(const Vector2DDocument& document)
{
    std::string seed;
    try {
        seed = document.SourcePath.empty() ? document.SourceName : document.SourcePath.u8string();
    } catch (...) {
        seed = document.SourceName;
    }
    if (seed.empty())
        seed = "Vector2DDocument";

    uint32_t value = HashString(seed) * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    value = (value >> 22u) ^ value;

    const float hue = (float)(value & 0xFFFFu) / 65535.0f;
    const float saturation = 0.62f + (float)((value >> 16u) & 0xFFu) / 255.0f * 0.25f;
    const float brightness = 0.82f + (float)((value >> 24u) & 0xFFu) / 255.0f * 0.16f;

    const float h = hue * 6.0f;
    const float c = brightness * saturation;
    const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    const float m = brightness - c;

    glm::vec3 rgb(0.0f);
    if (h < 1.0f)
        rgb = glm::vec3(c, x, 0.0f);
    else if (h < 2.0f)
        rgb = glm::vec3(x, c, 0.0f);
    else if (h < 3.0f)
        rgb = glm::vec3(0.0f, c, x);
    else if (h < 4.0f)
        rgb = glm::vec3(0.0f, x, c);
    else if (h < 5.0f)
        rgb = glm::vec3(x, 0.0f, c);
    else
        rgb = glm::vec3(c, 0.0f, x);

    return glm::vec4(rgb + glm::vec3(m), 1.0f);
}

size_t ArcSegmentCount(const Vector2DArc& arc)
{
    constexpr float kMaxArcStepRadians = 0.174532925f; // 10 degrees
    constexpr size_t kMinArcSegments = 2;
    constexpr size_t kMaxArcSegments = 96;
    if (arc.Radius <= 0.0f || std::abs(arc.SweepAngle) <= 0.000001f)
        return kMinArcSegments;

    const size_t count = (size_t)std::ceil(std::abs(arc.SweepAngle) / kMaxArcStepRadians);
    return std::max(kMinArcSegments, std::min(kMaxArcSegments, count));
}

glm::vec2 ArcPointAt(const Vector2DArc& arc, size_t index, size_t segmentCount)
{
    if (index == 0)
        return arc.Start;
    if (index >= segmentCount)
        return arc.End;

    const float t = (float)index / (float)segmentCount;
    const float angle = arc.StartAngle + arc.SweepAngle * t;
    return arc.Center + glm::vec2(std::cos(angle), std::sin(angle)) * arc.Radius;
}

void AppendPrimitiveLineVertices(const Vector2DPrimitive& primitive, std::vector<VertexColor>& vertices)
{
    if (primitive.Type == Vector2DPrimitiveType::Line)
    {
        vertices.emplace_back(glm::vec3(primitive.Line.Start, 0.0f), primitive.Line.Color);
        vertices.emplace_back(glm::vec3(primitive.Line.End, 0.0f), primitive.Line.Color);
        return;
    }

    const Vector2DArc& arc = primitive.Arc;
    const size_t segmentCount = ArcSegmentCount(arc);
    glm::vec2 previous = ArcPointAt(arc, 0, segmentCount);
    for (size_t i = 1; i <= segmentCount; ++i)
    {
        const glm::vec2 current = ArcPointAt(arc, i, segmentCount);
        vertices.emplace_back(glm::vec3(previous, 0.0f), arc.Color);
        vertices.emplace_back(glm::vec3(current, 0.0f), arc.Color);
        previous = current;
    }
}

void AppendPrimitivePointVertices(const Vector2DPrimitive& primitive, std::vector<VertexColor>& vertices)
{
    if (primitive.Type == Vector2DPrimitiveType::Line)
    {
        vertices.emplace_back(glm::vec3(primitive.Line.Start, 0.0f), primitive.Line.Color);
        vertices.emplace_back(glm::vec3(primitive.Line.End, 0.0f), primitive.Line.Color);
        return;
    }

    vertices.emplace_back(glm::vec3(primitive.Arc.Start, 0.0f), primitive.Arc.Color);
    vertices.emplace_back(glm::vec3(primitive.Arc.End, 0.0f), primitive.Arc.Color);
}

bool AppendElementPrimitiveLineVertices(const Object2D::Object2DElement& element,
    const std::vector<Vector2DPrimitive>& primitives,
    std::vector<VertexColor>& vertices)
{
    bool hasVertex = false;
    for (uint32_t primitiveIndex : element.PrimitiveIndices)
    {
        if (primitiveIndex >= primitives.size())
            continue;

        AppendPrimitiveLineVertices(primitives[(size_t)primitiveIndex], vertices);
        hasVertex = true;
    }
    return hasVertex;
}

bool AppendElementPrimitivePointVertices(const Object2D::Object2DElement& element,
    const std::vector<Vector2DPrimitive>& primitives,
    std::vector<VertexColor>& vertices)
{
    bool hasVertex = false;
    for (uint32_t primitiveIndex : element.PrimitiveIndices)
    {
        if (primitiveIndex >= primitives.size())
            continue;

        AppendPrimitivePointVertices(primitives[(size_t)primitiveIndex], vertices);
        hasVertex = true;
    }
    return hasVertex;
}

bool Mat4NearlyEqual(const glm::mat4& a, const glm::mat4& b)
{
    constexpr float kEpsilon = 0.000001f;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (std::abs(a[column][row] - b[column][row]) > kEpsilon)
                return false;
        }
    }
    return true;
}

}

Object2D::~Object2D()
{
    ReleaseElementGeometries();
    ReleaseBatchedGeometries();
    GeometryLibrary::Release(m_SelectedSubElementGeometry);
}

bool Object2D::LoadFromDocument(const Vector2DDocument& document)
{
    m_SourceName = document.SourceName;
    m_Lines = document.Lines;
    m_Primitives = document.Primitives;
    ReleaseElementGeometries();
    ReleaseBatchedGeometries();
    m_SubElements.clear();

    if (document.Elements.empty())
    {
        const uint32_t elementCount = (uint32_t)std::max(m_Lines.size(), m_Primitives.size());
        m_SubElements.reserve(elementCount);
        for (uint32_t i = 0; i < elementCount; ++i)
        {
            if (i < m_Lines.size())
                m_Lines[i].ElementIndex = i;
            Object2DElement element;
            element.Name = i < m_Lines.size() ? "Line" : "Primitive";
            if (i < m_Lines.size())
                element.LineIndices.push_back(i);
            if (i < m_Primitives.size())
                element.PrimitiveIndices.push_back(i);
            m_SubElements.push_back(std::move(element));
        }
    }
    else
    {
        m_SubElements.reserve(document.Elements.size());
        for (const Vector2DSubElement& sourceElement : document.Elements)
        {
            Object2DElement element;
            element.Name = sourceElement.Name;
            element.LineIndices = sourceElement.LineIndices;
            element.PrimitiveIndices = sourceElement.PrimitiveIndices;
            m_SubElements.push_back(std::move(element));
        }
    }

    const glm::vec4 displayColor = DisplayColorForFile(document);
    const glm::vec4 arcDisplayColor(1.0f);
    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)m_SubElements.size(); ++elementIndex)
    {
        for (uint32_t lineIndex : m_SubElements[(size_t)elementIndex].LineIndices)
        {
            if (lineIndex >= m_Lines.size())
                continue;
            m_Lines[(size_t)lineIndex].ElementIndex = elementIndex;
            m_Lines[(size_t)lineIndex].Color = m_Lines[(size_t)lineIndex].DisplayAsArc ? arcDisplayColor : displayColor;
        }
    }

    if (m_Primitives.empty())
    {
        m_Primitives.reserve(m_Lines.size());
        for (const Vector2DLine& line : m_Lines)
        {
            Vector2DPrimitive primitive;
            primitive.Type = Vector2DPrimitiveType::Line;
            primitive.Line = line;
            m_Primitives.push_back(primitive);
        }
    }

    for (Object2DElement& element : m_SubElements)
    {
        if (!element.PrimitiveIndices.empty() || m_Primitives.size() != m_Lines.size())
            continue;

        element.PrimitiveIndices.reserve(element.LineIndices.size());
        for (uint32_t lineIndex : element.LineIndices)
        {
            if (lineIndex < m_Primitives.size())
                element.PrimitiveIndices.push_back(lineIndex);
        }
    }

    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)m_SubElements.size(); ++elementIndex)
    {
        for (uint32_t primitiveIndex : m_SubElements[(size_t)elementIndex].PrimitiveIndices)
        {
            if (primitiveIndex >= m_Primitives.size())
                continue;

            Vector2DPrimitive& primitive = m_Primitives[(size_t)primitiveIndex];
            if (primitive.Type == Vector2DPrimitiveType::Arc)
            {
                primitive.Arc.ElementIndex = elementIndex;
                primitive.Arc.Color = arcDisplayColor;
            }
            else
            {
                primitive.Line.ElementIndex = elementIndex;
                primitive.Line.Color = displayColor;
            }
        }
    }

    std::vector<VertexColor> vertices;
    vertices.reserve(m_Primitives.size() * 2);
    for (const Vector2DPrimitive& primitive : m_Primitives)
        AppendPrimitiveLineVertices(primitive, vertices);

    if (vertices.empty())
        return false;

    for (Object2DElement& element : m_SubElements)
        BuildElementGeometry(element);

    m_LineShader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
    ClearSelectedSubElement();
    m_BatchedGeometryDirty = true;
    UpdateBoundsFromVertices(vertices);
    return true;
}

void Object2D::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass || m_SubElements.empty() || !m_LineShader)
        return;

    m_LineShader->Bind();
    m_LineShader->SetMat4("u_View", view);
    m_LineShader->SetMat4("u_Projection", proj);

    RenderCommand::EnableDepthTest(true);
    RenderCommand::SetLineWidth(2.0f);
    const glm::mat4 objectTransform = Transfm.GetMatrix();
    EnsureBatchedGeometries();
    Ref<VertexArray> batchedVertexArray = GeometryLibrary::Resolve(m_BatchedGeometry);
    const glm::vec2 viewportSize = Application::Get().GetViewportSize();
    const bool drewInstancedLines = !m_BatchedLineInstances.empty() &&
        RenderCommand::DrawInstancedLines(m_BatchedLineInstances.data(), (uint32_t)m_BatchedLineInstances.size(),
            view, proj, objectTransform, viewportSize);
    if (!drewInstancedLines && batchedVertexArray && m_BatchedVertexCount > 0)
    {
        m_LineShader->SetMat4("u_Model", objectTransform);
        RenderCommand::DrawLines(batchedVertexArray, m_BatchedVertexCount);
    }

    if (bPointVisible)
    {
        m_LineShader->Bind();
        m_LineShader->SetMat4("u_View", view);
        m_LineShader->SetMat4("u_Projection", proj);
        RenderCommand::SetPointSize(9.0f);
        Ref<VertexArray> batchedPointVertexArray = GeometryLibrary::Resolve(m_BatchedPointGeometry);
        if (batchedPointVertexArray && m_BatchedPointVertexCount > 0)
        {
            m_LineShader->SetMat4("u_Model", objectTransform);
            RenderCommand::DrawPoints(batchedPointVertexArray, m_BatchedPointVertexCount);
        }
        RenderCommand::SetPointSize(1.0f);
    }

    if (Application::Get().IsViewport2DEditMode() && !m_SelectedSubElementIndices.empty())
    {
        EnsurePatternShader();
        if (m_DashedLineShader)
        {
            m_DashedLineShader->Bind();
            m_DashedLineShader->SetMat4("u_View", view);
            m_DashedLineShader->SetMat4("u_Projection", proj);
            const glm::vec2 viewportSize = Application::Get().GetViewportSize();
            float pixelsPerWorld = 1.0f;
            if (viewportSize.y > 0.0f)
            {
                const float visibleWorldHeight = 2.0f / glm::length(glm::vec2(proj[1][1], 0.0f));
                if (visibleWorldHeight > 0.000001f)
                    pixelsPerWorld = viewportSize.y / visibleWorldHeight;
            }
            m_DashedLineShader->SetFloat("u_PixelsPerWorld", pixelsPerWorld);
            m_DashedLineShader->SetFloat("u_DashSpacing", 18.0f);
            m_DashedLineShader->SetFloat("u_DashLength", 10.0f);
            RenderCommand::SetLineWidth(3.0f);
            for (int selectedIndex : m_SelectedSubElementIndices)
            {
                const Object2DElement* selectedElement = selectedIndex >= 0 ? GetSubElement((size_t)selectedIndex) : nullptr;
                if (!selectedElement || !selectedElement->Visible || selectedElement->SelectionVertexCount == 0)
                    continue;
                Ref<VertexArray> selectedVertexArray = GeometryLibrary::Resolve(selectedElement->SelectionGeometry);
                if (!selectedVertexArray)
                    continue;

                m_DashedLineShader->SetMat4("u_Model", objectTransform * selectedElement->Transfm.GetMatrix());
                RenderCommand::DrawLines(selectedVertexArray, selectedElement->SelectionVertexCount);
            }
            RenderCommand::SetLineWidth(2.0f);
        }
    }
}

void Object2D::DrawPickup(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, int objectID, bool, float)
{
    if (!shader || m_SubElements.empty())
        return;

    shader->SetInt("u_ObjectID", objectID);
    shader->SetInt("u_XZInput", 0);
    shader->SetFloat("u_XZInputY", 0.0f);
    RenderCommand::SetLineWidth(7.0f);
    const glm::mat4 objectTransform = Transfm.GetMatrix();
    EnsureBatchedGeometries();
    Ref<VertexArray> batchedVertexArray = GeometryLibrary::Resolve(m_BatchedGeometry);
    if (batchedVertexArray && m_BatchedVertexCount > 0)
    {
        shader->SetMat4("u_Model", objectTransform);
        RenderCommand::DrawLines(batchedVertexArray, m_BatchedVertexCount);
    }
}

void Object2D::DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, bool, float)
{
    if (!shader || m_SubElements.empty())
        return;

    shader->SetInt("u_XZInput", 0);
    shader->SetFloat("u_XZInputY", 0.0f);
    RenderCommand::SetLineWidth(2.0f);
    const glm::mat4 objectTransform = Transfm.GetMatrix();
    EnsureBatchedGeometries();
    Ref<VertexArray> batchedVertexArray = GeometryLibrary::Resolve(m_BatchedGeometry);
    if (batchedVertexArray && m_BatchedVertexCount > 0)
    {
        shader->SetMat4("u_Model", objectTransform);
        RenderCommand::DrawLines(batchedVertexArray, m_BatchedVertexCount);
    }
}

void Object2D::DrawSubElementPickup(const glm::mat4& view, const glm::mat4& proj, const Ref<Shader>& shader)
{
    if (!shader || m_SubElements.empty())
        return;

    shader->SetInt("u_XZInput", 0);
    shader->SetFloat("u_XZInputY", 0.0f);
    RenderCommand::SetLineWidth(7.0f);
    const glm::mat4 objectTransform = Transfm.GetMatrix();

    for (uint32_t elementIndex = 0; elementIndex < (uint32_t)m_SubElements.size(); ++elementIndex)
    {
        const Object2DElement& element = m_SubElements[elementIndex];
        if (!element.Visible)
            continue;

        Ref<VertexArray> vertexArray = GeometryLibrary::Resolve(element.Geometry);
        if (!vertexArray || element.VertexCount == 0)
            continue;

        shader->SetInt("u_ObjectID", (int)elementIndex + 1);
        shader->SetMat4("u_Model", objectTransform * element.Transfm.GetMatrix());
        RenderCommand::DrawLines(vertexArray, element.VertexCount);
    }
}

void Object2D::SetSelectedSubElementIndex(int index)
{
    if (index < -1 || index >= (int)m_SubElements.size())
        index = -1;
    m_SelectedSubElementIndices.clear();
    if (index >= 0)
        m_SelectedSubElementIndices.push_back(index);
    m_SelectedSubElementIndex = index;
    RebuildSelectedSubElementGeometry();
}

void Object2D::SetSelectedSubElementIndices(const std::vector<int>& indices)
{
    m_SelectedSubElementIndices.clear();
    for (int index : indices)
    {
        if (index < 0 || index >= (int)m_SubElements.size())
            continue;
        if (std::find(m_SelectedSubElementIndices.begin(), m_SelectedSubElementIndices.end(), index) == m_SelectedSubElementIndices.end())
            m_SelectedSubElementIndices.push_back(index);
    }

    m_SelectedSubElementIndex = m_SelectedSubElementIndices.empty() ? -1 : m_SelectedSubElementIndices.front();
    RebuildSelectedSubElementGeometry();
}

std::string Object2D::GetSubElementName(size_t index) const
{
    const Object2DElement* element = GetSubElement(index);
    return element ? element->Name : std::string();
}

bool Object2D::IsSubElementVisible(size_t index) const
{
    const Object2DElement* element = GetSubElement(index);
    return element ? element->Visible : true;
}

void Object2D::SetSubElementVisible(size_t index, bool visible)
{
    Object2DElement* element = GetSubElement(index);
    if (!element || element->Visible == visible)
        return;

    element->Visible = visible;
    if (!visible && IsSubElementSelected((int)index))
        SetSelectedSubElementIndex(-1);
    m_BatchedGeometryDirty = true;
}

size_t Object2D::GetSubElementLineCount(size_t index) const
{
    const Object2DElement* element = GetSubElement(index);
    return element ? element->LineIndices.size() : 0;
}

bool Object2D::IsSubElementSelected(int index) const
{
    return std::find(m_SelectedSubElementIndices.begin(), m_SelectedSubElementIndices.end(), index) != m_SelectedSubElementIndices.end();
}

bool Object2D::GetObjectBounds(glm::vec3& minimum, glm::vec3& maximum) const
{
    minimum = glm::vec3(std::numeric_limits<float>::max());
    maximum = glm::vec3(std::numeric_limits<float>::lowest());
    bool hasPoint = false;

    const glm::mat4 objectTransform = Transfm.GetMatrix();
    for (const Object2DElement& element : m_SubElements)
    {
        if (!element.Visible)
            continue;

        const glm::mat4 transform = objectTransform * element.Transfm.GetMatrix();
        if (!element.HasBounds)
            continue;

        const glm::vec3 corners[] = {
            glm::vec3(element.LocalMinimum.x, element.LocalMinimum.y, 0.0f),
            glm::vec3(element.LocalMaximum.x, element.LocalMinimum.y, 0.0f),
            glm::vec3(element.LocalMaximum.x, element.LocalMaximum.y, 0.0f),
            glm::vec3(element.LocalMinimum.x, element.LocalMaximum.y, 0.0f)
        };
        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 point = glm::vec3(transform * glm::vec4(corner, 1.0f));
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        }
        hasPoint = true;
    }

    return hasPoint;
}

bool Object2D::GetSelectedSubElementBounds(glm::vec3& minimum, glm::vec3& maximum) const
{
    minimum = glm::vec3(std::numeric_limits<float>::max());
    maximum = glm::vec3(std::numeric_limits<float>::lowest());
    bool hasPoint = false;

    const glm::mat4 objectTransform = Transfm.GetMatrix();
    for (int selectedIndex : m_SelectedSubElementIndices)
    {
        if (selectedIndex < 0 || selectedIndex >= (int)m_SubElements.size())
            continue;

        const Object2DElement& element = m_SubElements[(size_t)selectedIndex];
        if (!element.Visible)
            continue;

        if (!element.HasBounds)
            continue;

        const glm::mat4 elementTransform = objectTransform * element.Transfm.GetMatrix();
        const glm::vec3 corners[] = {
            glm::vec3(element.LocalMinimum.x, element.LocalMinimum.y, 0.0f),
            glm::vec3(element.LocalMaximum.x, element.LocalMinimum.y, 0.0f),
            glm::vec3(element.LocalMaximum.x, element.LocalMaximum.y, 0.0f),
            glm::vec3(element.LocalMinimum.x, element.LocalMaximum.y, 0.0f)
        };
        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 point = glm::vec3(elementTransform * glm::vec4(corner, 1.0f));
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        }
        hasPoint = true;
    }

    return hasPoint;
}

Object2D::Object2DElement* Object2D::GetSubElement(size_t index)
{
    if (index >= m_SubElements.size())
        return nullptr;
    return &m_SubElements[index];
}

const Object2D::Object2DElement* Object2D::GetSubElement(size_t index) const
{
    if (index >= m_SubElements.size())
        return nullptr;
    return &m_SubElements[index];
}

Transform* Object2D::GetSubElementTransform(int index)
{
    Object2DElement* element = index >= 0 ? GetSubElement((size_t)index) : nullptr;
    return element ? &element->Transfm : nullptr;
}

Ref<VertexArray> Object2D::BuildVertexArray(const std::vector<VertexColor>& vertices) const
{
    Ref<VertexArray> vertexArray = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(const_cast<VertexColor*>(vertices.data())),
        (uint32_t)(vertices.size() * sizeof(VertexColor)));
    vertexBuffer->SetLayout({
        BufferElement(ShaderDataType::Float3, "a_Position", false),
        BufferElement(ShaderDataType::Float4, "a_Color", false)
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->Unbind();
    return vertexArray;
}

void Object2D::EnsurePatternShader()
{
    if (m_DashedLineShader)
        return;

    const std::string vertexSource = R"(
        #version 330 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;
        layout(location = 2) in float a_Distance;
        uniform mat4 u_View;
        uniform mat4 u_Projection;
        uniform mat4 u_Model;
        out vec4 v_Color;
        out float v_Distance;
        void main()
        {
            v_Color = a_Color;
            v_Distance = a_Distance;
            gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);
        }
    )";
    const std::string fragmentSource = R"(
        #version 330 core
        layout(location = 0) out vec4 color;
        in vec4 v_Color;
        in float v_Distance;
        uniform float u_PixelsPerWorld;
        uniform float u_DashSpacing;
        uniform float u_DashLength;
        void main()
        {
            float phase = mod(v_Distance * u_PixelsPerWorld, max(u_DashSpacing, 1.0));
            if (phase > u_DashLength)
                discard;
            color = v_Color;
        }
    )";
    m_DashedLineShader = Shader::Create("Object2DDashedSelection", vertexSource, fragmentSource);
}

void Object2D::ReleaseElementGeometries()
{
    for (Object2DElement& element : m_SubElements)
    {
        GeometryLibrary::Release(element.Geometry);
        GeometryLibrary::Release(element.PointGeometry);
        GeometryLibrary::Release(element.SelectionGeometry);
        element.Geometry = {};
        element.PointGeometry = {};
        element.SelectionGeometry = {};
        element.VertexCount = 0;
        element.PointVertexCount = 0;
        element.SelectionVertexCount = 0;
    }
}

void Object2D::ReleaseBatchedGeometries()
{
    GeometryLibrary::Release(m_BatchedGeometry);
    GeometryLibrary::Release(m_BatchedPointGeometry);
    m_BatchedGeometry = {};
    m_BatchedPointGeometry = {};
    m_BatchedVertexCount = 0;
    m_BatchedPointVertexCount = 0;
    m_BatchedLineInstances.clear();
    m_BatchedElementMatrices.clear();
    m_BatchedElementVisible.clear();
    m_BatchedGeometryDirty = true;
}

void Object2D::EnsureBatchedGeometries()
{
    bool needsRebuild = m_BatchedGeometryDirty ||
        m_BatchedElementMatrices.size() != m_SubElements.size() ||
        m_BatchedElementVisible.size() != m_SubElements.size();

    if (!needsRebuild)
    {
        for (size_t i = 0; i < m_SubElements.size(); ++i)
        {
            const Object2DElement& element = m_SubElements[i];
            if ((m_BatchedElementVisible[i] != 0) != element.Visible ||
                !Mat4NearlyEqual(m_BatchedElementMatrices[i], element.Transfm.GetMatrix()))
            {
                needsRebuild = true;
                break;
            }
        }
    }

    if (!needsRebuild)
        return;

    GeometryLibrary::Release(m_BatchedGeometry);
    GeometryLibrary::Release(m_BatchedPointGeometry);
    m_BatchedGeometry = {};
    m_BatchedPointGeometry = {};
    m_BatchedVertexCount = 0;
    m_BatchedPointVertexCount = 0;
    m_BatchedLineInstances.clear();
    m_BatchedElementMatrices.clear();
    m_BatchedElementVisible.clear();
    m_BatchedElementMatrices.reserve(m_SubElements.size());
    m_BatchedElementVisible.reserve(m_SubElements.size());

    size_t lineVertexReserve = 0;
    size_t pointVertexReserve = 0;
    for (const Object2DElement& element : m_SubElements)
    {
        m_BatchedElementMatrices.push_back(element.Transfm.GetMatrix());
        m_BatchedElementVisible.push_back(element.Visible ? 1 : 0);
        if (!element.Visible)
            continue;

        lineVertexReserve += element.VertexCount;
        pointVertexReserve += element.PointVertexCount;
    }

    std::vector<VertexColor> lineVertices;
    lineVertices.reserve(lineVertexReserve);
    std::vector<VertexColor> pointVertices;
    pointVertices.reserve(pointVertexReserve);

    for (const Object2DElement& element : m_SubElements)
    {
        if (!element.Visible || element.VertexCount == 0)
            continue;

        std::vector<VertexColor> rawVertices;
        rawVertices.reserve(std::max(element.PrimitiveIndices.size(), element.LineIndices.size()) * 2);
        if (element.PrimitiveIndices.empty() || !AppendElementPrimitiveLineVertices(element, m_Primitives, rawVertices))
        {
            for (uint32_t lineIndex : element.LineIndices)
            {
                if (lineIndex >= m_Lines.size())
                    continue;
                const Vector2DLine& line = m_Lines[lineIndex];
                rawVertices.emplace_back(glm::vec3(line.Start, 0.0f), line.Color);
                rawVertices.emplace_back(glm::vec3(line.End, 0.0f), line.Color);
            }
        }

        const glm::mat4 elementTransform = element.Transfm.GetMatrix();
        for (const VertexColor& vertex : rawVertices)
        {
            const glm::vec2 point(vertex.Position.x, vertex.Position.y);
            const glm::vec3 localPoint(point - element.Center, 0.0f);
            lineVertices.emplace_back(glm::vec3(elementTransform * glm::vec4(localPoint, 1.0f)), vertex.Color);
        }

        if (element.PointVertexCount == 0)
            continue;

        std::vector<VertexColor> rawPointVertices;
        rawPointVertices.reserve(std::max(element.PrimitiveIndices.size(), element.LineIndices.size()) * 2);
        if (element.PrimitiveIndices.empty() || !AppendElementPrimitivePointVertices(element, m_Primitives, rawPointVertices))
        {
            for (uint32_t lineIndex : element.LineIndices)
            {
                if (lineIndex >= m_Lines.size())
                    continue;
                const Vector2DLine& line = m_Lines[lineIndex];
                rawPointVertices.emplace_back(glm::vec3(line.Start, 0.0f), line.Color);
                rawPointVertices.emplace_back(glm::vec3(line.End, 0.0f), line.Color);
            }
        }

        for (const VertexColor& vertex : rawPointVertices)
        {
            const glm::vec2 point(vertex.Position.x, vertex.Position.y);
            const glm::vec3 localPoint(point - element.Center, 0.0f);
            pointVertices.emplace_back(glm::vec3(elementTransform * glm::vec4(localPoint, 1.0f)), vertex.Color);
        }
    }

    if (!lineVertices.empty())
    {
        m_BatchedLineInstances.reserve(lineVertices.size() / 2);
        for (size_t i = 0; i + 1 < lineVertices.size(); i += 2)
        {
            RendererLineInstance instance;
            instance.Start = glm::vec4(lineVertices[i].Position, 1.0f);
            instance.End = glm::vec4(lineVertices[i + 1].Position, 1.0f);
            instance.Color = lineVertices[i].Color;
            m_BatchedLineInstances.push_back(instance);
        }

        m_BatchedGeometry = GeometryLibrary::Register(BuildVertexArray(lineVertices));
        m_BatchedVertexCount = (uint32_t)lineVertices.size();
    }

    if (!pointVertices.empty())
    {
        m_BatchedPointGeometry = GeometryLibrary::Register(BuildVertexArray(pointVertices));
        m_BatchedPointVertexCount = (uint32_t)pointVertices.size();
    }

    m_BatchedGeometryDirty = false;
}

void Object2D::BuildElementGeometry(Object2DElement& element)
{
    GeometryLibrary::Release(element.Geometry);
    GeometryLibrary::Release(element.PointGeometry);
    GeometryLibrary::Release(element.SelectionGeometry);
    element.Geometry = {};
    element.PointGeometry = {};
    element.SelectionGeometry = {};
    element.VertexCount = 0;
    element.PointVertexCount = 0;
    element.SelectionVertexCount = 0;
    element.Center = glm::vec2(0.0f);
    element.LocalMinimum = glm::vec2(0.0f);
    element.LocalMaximum = glm::vec2(0.0f);
    element.HasBounds = false;

    std::vector<VertexColor> rawVertices;
    rawVertices.reserve(std::max(element.PrimitiveIndices.size(), element.LineIndices.size()) * 2);
    std::vector<VertexColor> rawPointVertices;
    rawPointVertices.reserve(std::max(element.PrimitiveIndices.size(), element.LineIndices.size()) * 2);

    if (!element.PrimitiveIndices.empty() && AppendElementPrimitiveLineVertices(element, m_Primitives, rawVertices))
    {
        AppendElementPrimitivePointVertices(element, m_Primitives, rawPointVertices);
    }
    else
    {
        for (uint32_t lineIndex : element.LineIndices)
        {
            if (lineIndex >= m_Lines.size())
                continue;

            const Vector2DLine& line = m_Lines[lineIndex];
            rawVertices.emplace_back(glm::vec3(line.Start, 0.0f), line.Color);
            rawVertices.emplace_back(glm::vec3(line.End, 0.0f), line.Color);
            rawPointVertices.emplace_back(glm::vec3(line.Start, 0.0f), line.Color);
            rawPointVertices.emplace_back(glm::vec3(line.End, 0.0f), line.Color);
        }
    }

    if (rawVertices.empty())
        return;

    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    for (const VertexColor& vertex : rawVertices)
    {
        const glm::vec2 point(vertex.Position.x, vertex.Position.y);
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }

    element.Center = (minimum + maximum) * 0.5f;
    element.Transfm.translation = glm::vec3(element.Center, 0.0f);
    element.LocalMinimum = minimum - element.Center;
    element.LocalMaximum = maximum - element.Center;
    element.HasBounds = true;

    std::vector<VertexColor> vertices;
    vertices.reserve(rawVertices.size());
    std::vector<PatternVertex> selectionVertices;
    selectionVertices.reserve(rawVertices.size());
    float distance = 0.0f;
    for (size_t i = 0; i + 1 < rawVertices.size(); i += 2)
    {
        const glm::vec2 start(rawVertices[i].Position.x, rawVertices[i].Position.y);
        const glm::vec2 end(rawVertices[i + 1].Position.x, rawVertices[i + 1].Position.y);
        const glm::vec3 localStart(start - element.Center, 0.0f);
        const glm::vec3 localEnd(end - element.Center, 0.0f);
        vertices.emplace_back(localStart, rawVertices[i].Color);
        vertices.emplace_back(localEnd, rawVertices[i + 1].Color);
        const float length = glm::length(end - start);
        selectionVertices.push_back({ localStart, glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance });
        selectionVertices.push_back({ localEnd, glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance + length });
        distance += length;
    }
    if (vertices.empty())
        return;

    element.Geometry = GeometryLibrary::Register(BuildVertexArray(vertices));
    element.VertexCount = (uint32_t)vertices.size();

    std::vector<VertexColor> pointVertices;
    pointVertices.reserve(rawPointVertices.size());
    for (const VertexColor& pointVertex : rawPointVertices)
    {
        const glm::vec2 point(pointVertex.Position.x, pointVertex.Position.y);
        pointVertices.emplace_back(glm::vec3(point - element.Center, 0.0f), pointVertex.Color);
    }
    if (!pointVertices.empty())
    {
        element.PointGeometry = GeometryLibrary::Register(BuildVertexArray(pointVertices));
        element.PointVertexCount = (uint32_t)pointVertices.size();
    }

    Ref<VertexArray> selectionVertexArray = VertexArray::Create();
    auto selectionVertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(selectionVertices.data()),
        (uint32_t)(selectionVertices.size() * sizeof(PatternVertex)));
    selectionVertexBuffer->SetLayout({
        BufferElement(ShaderDataType::Float3, "a_Position", false),
        BufferElement(ShaderDataType::Float4, "a_Color", false),
        BufferElement(ShaderDataType::Float, "a_Distance", false)
    });
    selectionVertexArray->AddVertexBuffer(selectionVertexBuffer);
    selectionVertexArray->Unbind();
    element.SelectionGeometry = GeometryLibrary::Register(selectionVertexArray);
    element.SelectionVertexCount = (uint32_t)selectionVertices.size();
}

void Object2D::RebuildSelectedSubElementGeometry()
{
    GeometryLibrary::Release(m_SelectedSubElementGeometry);
    m_SelectedSubElementGeometry = {};
    m_SelectedSubElementVertexCount = 0;
    if (m_SelectedSubElementIndex < 0 || m_SelectedSubElementIndex >= (int)m_SubElements.size())
        return;

    const Object2DElement& element = m_SubElements[(size_t)m_SelectedSubElementIndex];
    std::vector<VertexColor> rawVertices;
    rawVertices.reserve(std::max(element.PrimitiveIndices.size(), element.LineIndices.size()) * 2);
    if (element.PrimitiveIndices.empty() || !AppendElementPrimitiveLineVertices(element, m_Primitives, rawVertices))
    {
        for (uint32_t lineIndex : element.LineIndices)
        {
            if (lineIndex >= m_Lines.size())
                continue;
            const Vector2DLine& line = m_Lines[lineIndex];
            rawVertices.emplace_back(glm::vec3(line.Start, 0.0f), line.Color);
            rawVertices.emplace_back(glm::vec3(line.End, 0.0f), line.Color);
        }
    }

    std::vector<PatternVertex> vertices;
    vertices.reserve(rawVertices.size());
    float distance = 0.0f;
    for (size_t i = 0; i + 1 < rawVertices.size(); i += 2)
    {
        const glm::vec2 start(rawVertices[i].Position.x, rawVertices[i].Position.y);
        const glm::vec2 end(rawVertices[i + 1].Position.x, rawVertices[i + 1].Position.y);
        const float length = glm::length(end - start);
        vertices.push_back({ glm::vec3(start - element.Center, 0.0f), glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance });
        vertices.push_back({ glm::vec3(end - element.Center, 0.0f), glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance + length });
        distance += length;
    }
    if (vertices.empty())
        return;

    Ref<VertexArray> vertexArray = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()),
        (uint32_t)(vertices.size() * sizeof(PatternVertex)));
    vertexBuffer->SetLayout({
        BufferElement(ShaderDataType::Float3, "a_Position", false),
        BufferElement(ShaderDataType::Float4, "a_Color", false),
        BufferElement(ShaderDataType::Float, "a_Distance", false)
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->Unbind();
    m_SelectedSubElementGeometry = GeometryLibrary::Register(vertexArray);
    m_SelectedSubElementVertexCount = (uint32_t)vertices.size();
}

void Object2D::UpdateBoundsFromVertices(const std::vector<VertexColor>& vertices)
{
    Bounds = {};
    if (vertices.empty())
        return;

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const VertexColor& vertex : vertices)
    {
        minimum = glm::min(minimum, vertex.Position);
        maximum = glm::max(maximum, vertex.Position);
    }

    Bounds.Center = (minimum + maximum) * 0.5f;
    for (const VertexColor& vertex : vertices)
        Bounds.Radius = std::max(Bounds.Radius, glm::length(vertex.Position - Bounds.Center));
    Bounds.Valid = true;
}
