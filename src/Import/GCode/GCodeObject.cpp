#include "GCodeObject.h"

#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>
#include <Renderer/VertexArray.h>
#include <Renderer/VertexDesc.h>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace
{
constexpr float kPi = 3.14159265358979323846f;

std::string ReadTextFile(const std::filesystem::path& filepath)
{
    std::ifstream input(filepath, std::ios::binary);
    if (!input)
        return {};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

glm::vec3 SafeNormal(const glm::vec3& value, const glm::vec3& fallback)
{
    const float len = glm::length(value);
    return len > 0.000001f ? value / len : fallback;
}
}

GCodeObject::GCodeObject()
{
    EnsureShader();
}

bool GCodeObject::LoadFromFile(const std::filesystem::path& filepath)
{
    const std::string content = ReadTextFile(filepath);
    if (content.empty())
        return false;
    return LoadFromContent(content, filepath.u8string());
}

bool GCodeObject::LoadFromContent(const std::string& content, const std::string& sourceName)
{
    try
    {
        m_Program.LoadContent(content, sourceName);
    }
    catch (const std::exception& e)
    {
        ERROR("Failed to parse GCode {}: {}", sourceName, e.what());
        return false;
    }

    m_SourceName = sourceName;
    if (!BuildLineGeometry())
        return false;
    BuildToolMeshes();
    SetProgress(1.0f);
    UpdateBounds();
    return true;
}

void GCodeObject::EnsureShader()
{
    if (m_LineShader)
        return;

    const std::string vertex = R"(
        #version 330 core
        layout(location = 0) in vec4 a_PositionWithAngle;
        layout(location = 1) in float a_ColorType;
        layout(location = 2) in float a_LineNo;
        layout(location = 3) in float a_ToolNumber;
        layout(location = 4) in float a_ToolpathNumber;

        uniform mat4 u_Model;
        uniform mat4 u_View;
        uniform mat4 u_Projection;

        out float v_ColorType;
        out float v_LineNo;
        out float v_ToolNumber;
        out vec3 v_WorldPos;
        out float v_LinearDepth;

        void main()
        {
            vec4 world = u_Model * vec4(a_PositionWithAngle.xyz, 1.0);
            vec4 viewPos = u_View * world;
            gl_Position = u_Projection * viewPos;
            v_ColorType = a_ColorType;
            v_LineNo = a_LineNo;
            v_ToolNumber = a_ToolNumber;
            v_WorldPos = world.xyz;
            v_LinearDepth = max(-viewPos.z, 0.0001);
        }
    )";

    const std::string fragment = R"(
        #version 330 core
        layout(location = 0) out vec4 color;

        uniform int u_DisplayIndex;
        uniform int u_HideFastMoves;

        in float v_ColorType;
        in float v_LineNo;
        in float v_ToolNumber;
        in vec3 v_WorldPos;
        in float v_LinearDepth;

        void main()
        {
            if (u_DisplayIndex >= 0 && v_LineNo > float(u_DisplayIndex))
                discard;
            if (u_HideFastMoves != 0 && v_ColorType < 0.5)
                discard;

            vec4 baseColor;
            if (v_ColorType < 0.5)
                baseColor = vec4(1.0, 0.12, 0.08, 1.0);
            else if (v_ColorType < 1.5)
                baseColor = vec4(0.08, 0.88, 0.18, 1.0);
            else
                baseColor = vec4(0.15, 0.45, 1.0, 1.0);

            float depthMetric = log2(v_LinearDepth + 10.0);
            float depthSlope = abs(dFdx(depthMetric)) + abs(dFdy(depthMetric));
            float shade = exp(-60.0 * depthSlope * 4.0);
            shade = clamp(mix(1.0, shade, 0.6), 0.45, 1.0);
            color = vec4(baseColor.rgb * shade, baseColor.a);
        }
    )";

    m_LineShader = Shader::Create("GCodeLine", vertex, fragment);
}

bool GCodeObject::BuildLineGeometry()
{
    m_LineVertices.clear();
    m_SegmentToMoveIndex.clear();
    m_TotalDistance = 0.0f;

    const auto& moves = m_Program.GetMoves();
    if (moves.size() < 2)
        return false;

    int segmentIndex = 0;
    for (size_t i = 1; i < moves.size(); ++i)
    {
        const gcode::Move& prev = moves[i - 1];
        const gcode::Move& curr = moves[i];
        if (prev.ToolNumber <= 0 || curr.ToolNumber <= 0 || prev.ToolNumber != curr.ToolNumber)
            continue;
        if (prev.ToolpathNumber != curr.ToolpathNumber)
            continue;
        if (curr.Code != "G0" && curr.Code != "G1")
            continue;

        const float colorType = curr.IsLaserPath ? 2.0f : (curr.Code == "G1" ? 1.0f : 0.0f);
        m_LineVertices.push_back({ glm::vec4(prev.Position, prev.RotaryDegrees.x), colorType,
            (float)segmentIndex, (float)curr.ToolNumber, (float)curr.ToolpathNumber });
        m_LineVertices.push_back({ glm::vec4(curr.Position, curr.RotaryDegrees.x), colorType,
            (float)segmentIndex, (float)curr.ToolNumber, (float)curr.ToolpathNumber });
        m_SegmentToMoveIndex.push_back(i);
        m_TotalDistance += glm::distance(prev.Position, curr.Position);
        ++segmentIndex;
    }

    m_TotalSegments = segmentIndex;
    m_DisplayIndex = m_TotalSegments;
    if (m_LineVertices.empty())
        return false;

    m_LineVertexArray = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(m_LineVertices.data()),
        (uint32_t)(m_LineVertices.size() * sizeof(GCodeLineVertex)));
    vertexBuffer->SetLayout({
        BufferElement(ShaderDataType::Float4, "a_PositionWithAngle", false),
        BufferElement(ShaderDataType::Float, "a_ColorType", false),
        BufferElement(ShaderDataType::Float, "a_LineNo", false),
        BufferElement(ShaderDataType::Float, "a_ToolNumber", false),
        BufferElement(ShaderDataType::Float, "a_ToolpathNumber", false)
    });
    m_LineVertexArray->AddVertexBuffer(vertexBuffer);
    m_LineVertexArray->Unbind();
    return true;
}

void GCodeObject::BuildToolMeshes()
{
    m_ToolMeshes.clear();
    for (const gcode::ToolInfo& tool : m_Program.GetTools())
    {
        Ref<Mesh> mesh = CreateToolMesh(tool);
        if (mesh)
            m_ToolMeshes[tool.Number] = mesh;
    }
}

Ref<Mesh> GCodeObject::CreateToolMesh(const gcode::ToolInfo& tool) const
{
    if (tool.Xs.empty() || tool.Ys.empty() || tool.Xs.size() != tool.Ys.size())
        return nullptr;

    constexpr int radialSegments = 32;
    std::vector<float> xs = tool.Xs;
    std::vector<float> ys = tool.Ys;
    xs.push_back(xs.back());
    ys.push_back(ys.back() + 20.0f);

    std::vector<VertexNormalTexture> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(xs.size() * radialSegments);

    for (size_t profile = 0; profile < xs.size(); ++profile)
    {
        const float radius = std::max(xs[profile], 0.001f);
        const float z = ys[profile];
        for (int seg = 0; seg < radialSegments; ++seg)
        {
            const float angle = 2.0f * kPi * (float)seg / (float)radialSegments;
            const glm::vec3 pos(radius * std::cos(angle), radius * std::sin(angle), z);
            const glm::vec3 normal = SafeNormal(glm::vec3(pos.x, pos.y, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            vertices.emplace_back(pos, normal, glm::vec2((float)seg / (float)radialSegments, (float)profile / (float)xs.size()));
        }
    }

    for (uint32_t profile = 0; profile + 1 < (uint32_t)xs.size(); ++profile)
    {
        for (uint32_t seg = 0; seg < radialSegments; ++seg)
        {
            const uint32_t nextSeg = (seg + 1) % radialSegments;
            const uint32_t a = profile * radialSegments + seg;
            const uint32_t b = profile * radialSegments + nextSeg;
            const uint32_t c = (profile + 1) * radialSegments + nextSeg;
            const uint32_t d = (profile + 1) * radialSegments + seg;
            indices.insert(indices.end(), { a, b, c, c, d, a });
        }
    }

    Ref<Mesh> mesh = CreateRef<Mesh>();
    mesh->VertexObject = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()),
        (uint32_t)(vertices.size() * sizeof(VertexNormalTexture)));
    vertexBuffer->SetLayout({
        BufferElement(ShaderDataType::Float3, "a_Position", false),
        BufferElement(ShaderDataType::Float3, "a_Normal", false),
        BufferElement(ShaderDataType::Float2, "a_TexCoord", false)
    });
    mesh->VertexObject->AddVertexBuffer(vertexBuffer);
    mesh->VertexObject->SetIndexBuffer(IndexBuffer::Create(indices.data(), (uint32_t)indices.size()));
    mesh->VertexObject->Unbind();
    Ref<MaterialPBR> material = CreateRef<MaterialPBR>();
    material->Albedo = glm::vec3(0.95f, 0.82f, 0.12f);
    material->Metallic = 0.2f;
    material->Roughness = 0.35f;
    mesh->Mat = material;
    mesh->TraceVertices = std::move(vertices);
    mesh->TraceIndices = std::move(indices);
    mesh->UpdateBoundingSphere();
    return mesh;
}

void GCodeObject::SetProgress(float progress)
{
    m_Progress = glm::clamp(progress, 0.0f, 1.0f);
    m_DisplayIndex = m_TotalSegments > 0 ? (int)std::floor(m_Progress * (float)m_TotalSegments) : 0;
    UpdateToolPosition();
}

void GCodeObject::Reset()
{
    m_Playing = false;
    SetProgress(0.0f);
}

void GCodeObject::Update(float deltaTime)
{
    if (!m_Playing || m_TotalSegments <= 0)
        return;
    const float deltaSegments = std::max(m_PlaybackSpeed, 1.0f) * deltaTime;
    SetProgress(m_Progress + deltaSegments / (float)m_TotalSegments);
    if (m_Progress >= 1.0f)
        m_Playing = false;
}

void GCodeObject::UpdateToolPosition()
{
    m_CurrentToolMesh = nullptr;
    if (m_SegmentToMoveIndex.empty())
        return;

    const int segment = glm::clamp(m_DisplayIndex - 1, 0, (int)m_SegmentToMoveIndex.size() - 1);
    const size_t moveIndex = m_SegmentToMoveIndex[segment];
    const auto& moves = m_Program.GetMoves();
    if (moveIndex >= moves.size())
        return;

    const gcode::Move& move = moves[moveIndex];
    const auto it = m_ToolMeshes.find(move.ToolNumber);
    if (it == m_ToolMeshes.end())
        return;

    m_CurrentToolMesh = it->second;
    m_CurrentToolMesh->Transfm.translation = move.Position;
    m_CurrentToolMesh->Transfm.rotation = glm::angleAxis(glm::radians(-move.RotaryDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    m_CurrentToolMesh->Transfm.scale = glm::vec3(1.0f);
}

void GCodeObject::UpdateBounds()
{
    Bounds = {};
    const gcode::BoundingBox& bounds = m_Program.GetDisplayBounds();
    if (!bounds.Valid)
        return;

    Bounds.Center = (bounds.Min + bounds.Max) * 0.5f;
    Bounds.Radius = glm::length(bounds.Max - Bounds.Center);
    Bounds.Valid = true;
}

void GCodeObject::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass || !m_LineVertexArray || !m_LineShader || m_LineVertices.empty())
        return;

    m_LineShader->Bind();
    m_LineShader->SetMat4("u_Model", Transfm.GetMatrix());
    m_LineShader->SetMat4("u_View", view);
    m_LineShader->SetMat4("u_Projection", proj);
    m_LineShader->SetInt("u_DisplayIndex", m_DisplayIndex);
    m_LineShader->SetInt("u_HideFastMoves", m_ShowFastMoves ? 0 : 1);

    RenderCommand::SetLineWidth(2.0f);
    RenderCommand::DrawLines(m_LineVertexArray, (uint32_t)m_LineVertices.size());

    if (m_ShowTool && m_CurrentToolMesh)
        m_CurrentToolMesh->Draw(view, proj, Transfm.GetMatrix(), 1.0f, false);
}
