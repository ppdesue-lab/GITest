#include "stdsfx.h"
#include "Object2D.h"

#include <Application.h>
#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>

#include <limits>

Object2D::~Object2D()
{
    ReleaseElementGeometries();
    GeometryLibrary::Release(m_SelectedSubElementGeometry);
}

bool Object2D::LoadFromDocument(const Vector2DDocument& document)
{
    m_SourceName = document.SourceName;
    m_Lines = document.Lines;
    ReleaseElementGeometries();
    m_SubElements.clear();

    if (document.Elements.empty())
    {
        m_SubElements.reserve(m_Lines.size());
        for (uint32_t i = 0; i < (uint32_t)m_Lines.size(); ++i)
        {
            m_Lines[i].ElementIndex = i;
            Object2DElement element;
            element.Name = "Line";
            element.LineIndices.push_back(i);
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
            m_SubElements.push_back(std::move(element));
        }
    }

    std::vector<VertexColor> vertices;
    vertices.reserve(m_Lines.size() * 2);
    for (const Vector2DLine& line : m_Lines)
    {
        vertices.emplace_back(glm::vec3(line.Start, 0.0f), line.Color);
        vertices.emplace_back(glm::vec3(line.End, 0.0f), line.Color);
    }

    if (vertices.empty())
        return false;

    for (Object2DElement& element : m_SubElements)
        BuildElementGeometry(element);

    m_LineShader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
    ClearSelectedSubElement();
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
    RenderCommand::SetLineWidth(1.0f);
    const glm::mat4 objectTransform = Transfm.GetMatrix();
    for (const Object2DElement& element : m_SubElements)
    {
        if (!element.Visible)
            continue;

        Ref<VertexArray> vertexArray = GeometryLibrary::Resolve(element.Geometry);
        if (!vertexArray || element.VertexCount == 0)
            continue;

        m_LineShader->SetMat4("u_Model", objectTransform * element.Transfm.GetMatrix());
        RenderCommand::DrawLines(vertexArray, element.VertexCount);
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
            RenderCommand::SetLineWidth(1.0f);
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
    for (const Object2DElement& element : m_SubElements)
    {
        if (!element.Visible)
            continue;

        Ref<VertexArray> vertexArray = GeometryLibrary::Resolve(element.Geometry);
        if (!vertexArray || element.VertexCount == 0)
            continue;

        shader->SetMat4("u_Model", objectTransform * element.Transfm.GetMatrix());
        RenderCommand::DrawLines(vertexArray, element.VertexCount);
    }
}

void Object2D::DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
    const Ref<Shader>& shader, bool, float)
{
    if (!shader || m_SubElements.empty())
        return;

    shader->SetInt("u_XZInput", 0);
    shader->SetFloat("u_XZInputY", 0.0f);
    RenderCommand::SetLineWidth(1.0f);
    const glm::mat4 objectTransform = Transfm.GetMatrix();
    for (const Object2DElement& element : m_SubElements)
    {
        if (!element.Visible)
            continue;

        Ref<VertexArray> vertexArray = GeometryLibrary::Resolve(element.Geometry);
        if (!vertexArray || element.VertexCount == 0)
            continue;

        shader->SetMat4("u_Model", objectTransform * element.Transfm.GetMatrix());
        RenderCommand::DrawLines(vertexArray, element.VertexCount);
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

bool Object2D::IsSubElementSelected(int index) const
{
    return std::find(m_SelectedSubElementIndices.begin(), m_SelectedSubElementIndices.end(), index) != m_SelectedSubElementIndices.end();
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
        GeometryLibrary::Release(element.SelectionGeometry);
        element.Geometry = {};
        element.SelectionGeometry = {};
        element.VertexCount = 0;
        element.SelectionVertexCount = 0;
    }
}

void Object2D::BuildElementGeometry(Object2DElement& element)
{
    GeometryLibrary::Release(element.Geometry);
    element.Geometry = {};
    element.VertexCount = 0;
    element.Center = glm::vec2(0.0f);

    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    bool hasPoint = false;
    for (uint32_t lineIndex : element.LineIndices)
    {
        if (lineIndex >= m_Lines.size())
            continue;
        const Vector2DLine& line = m_Lines[lineIndex];
        minimum = glm::min(minimum, glm::min(line.Start, line.End));
        maximum = glm::max(maximum, glm::max(line.Start, line.End));
        hasPoint = true;
    }
    if (!hasPoint)
        return;

    element.Center = (minimum + maximum) * 0.5f;
    element.Transfm.translation = glm::vec3(element.Center, 0.0f);

    std::vector<VertexColor> vertices;
    vertices.reserve(element.LineIndices.size() * 2);
    std::vector<PatternVertex> selectionVertices;
    selectionVertices.reserve(element.LineIndices.size() * 2);
    float distance = 0.0f;
    for (uint32_t lineIndex : element.LineIndices)
    {
        if (lineIndex >= m_Lines.size())
            continue;
        const Vector2DLine& line = m_Lines[lineIndex];
        vertices.emplace_back(glm::vec3(line.Start - element.Center, 0.0f), line.Color);
        vertices.emplace_back(glm::vec3(line.End - element.Center, 0.0f), line.Color);
        const float length = glm::length(line.End - line.Start);
        selectionVertices.push_back({ glm::vec3(line.Start - element.Center, 0.0f), glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance });
        selectionVertices.push_back({ glm::vec3(line.End - element.Center, 0.0f), glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance + length });
        distance += length;
    }
    if (vertices.empty())
        return;

    element.Geometry = GeometryLibrary::Register(BuildVertexArray(vertices));
    element.VertexCount = (uint32_t)vertices.size();

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
    std::vector<PatternVertex> vertices;
    vertices.reserve(element.LineIndices.size() * 2);
    float distance = 0.0f;
    for (uint32_t lineIndex : element.LineIndices)
    {
        if (lineIndex >= m_Lines.size())
            continue;
        const Vector2DLine& line = m_Lines[lineIndex];
        const float length = glm::length(line.End - line.Start);
        vertices.push_back({ glm::vec3(line.Start - element.Center, 0.0f), glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance });
        vertices.push_back({ glm::vec3(line.End - element.Center, 0.0f), glm::vec4(1.0f, 0.85f, 0.05f, 1.0f), distance + length });
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
