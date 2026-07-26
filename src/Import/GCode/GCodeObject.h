#pragma once

#include "CNCParser.h"
#include <Object3D.h>
#include <Renderer/Renderer.h>
#include <Renderer/Shader.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct GCodeLineVertex
{
    glm::vec4 PositionWithAngle;
    float ColorType = 0.0f;
    float LineNo = 0.0f;
    float ToolNumber = 0.0f;
    float ToolpathNumber = 0.0f;
};

class GCodeObject : public Object3D
{
public:
    GCodeObject();

    bool LoadFromFile(const std::filesystem::path& filepath, bool useInstancedRendering = false);
    bool LoadFromContent(const std::string& content, const std::string& sourceName = "<content>",
        bool useInstancedRendering = false);
    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;

    void SetProgress(float progress);
    float GetProgress() const { return m_Progress; }
    int GetDisplayIndex() const { return m_DisplayIndex; }
    int GetTotalSegmentCount() const { return m_TotalSegments; }
    float GetTotalDistance() const { return m_TotalDistance; }

    void SetPlaying(bool playing) { m_Playing = playing; }
    bool IsPlaying() const { return m_Playing; }
    void TogglePlaying() { m_Playing = !m_Playing; }
    void Reset();
    void Update(float deltaTime);

    bool& ShowFastMoves() { return m_ShowFastMoves; }
    bool& ShowTool() { return m_ShowTool; }
    float& PlaybackSpeed() { return m_PlaybackSpeed; }
    const std::string& GetSourceName() const { return m_SourceName; }
    const gcode::CNCProgram& GetProgram() const { return m_Program; }

private:
    bool BuildLineGeometry();
    void BuildToolMeshes();
    Ref<Mesh> CreateToolMesh(const gcode::ToolInfo& tool) const;
    void UpdateToolPosition();
    void UpdateBounds();
    void EnsureShader();

    gcode::CNCProgram m_Program;
    std::string m_SourceName;
    std::vector<GCodeLineVertex> m_LineVertices;
    std::vector<RendererLineInstance> m_LineInstances;
    Ref<RendererLineInstanceBuffer> m_LineInstanceBuffer;
    Ref<RendererLineInstanceBuffer> m_NonFastLineInstanceBuffer;
    uint32_t m_NonFastLineInstanceCount = 0;
    std::vector<uint32_t> m_NonFastDisplayCounts;
    std::vector<size_t> m_SegmentToMoveIndex;
    Ref<VertexArray> m_LineVertexArray;
    Ref<Shader> m_LineShader;
    std::map<int, Ref<Mesh>> m_ToolMeshes;
    Ref<Mesh> m_CurrentToolMesh;

    float m_Progress = 1.0f;
    int m_DisplayIndex = 0;
    int m_TotalSegments = 0;
    float m_TotalDistance = 0.0f;
    float m_PlaybackSpeed = 120.0f;
    bool m_Playing = false;
    bool m_ShowFastMoves = true;
    bool m_ShowTool = true;
    bool m_UseInstancedRendering = false;
};
