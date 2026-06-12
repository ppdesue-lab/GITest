#include "Editor/ObjectInspector.h"

#include <Import/GCode/GCodeObject.h>
#include <Renderer/TerrainCDLOD.h>
#include <Renderer/VolumeObject.h>

#include <imgui.h>

namespace
{
class MeshDisplayInspector : public ObjectInspector
{
public:
    bool CanInspect(const Object3D& object) const override
    {
        return !object.Meshes.empty();
    }

    void Draw(Object3D& object) override
    {
        ImGui::SeparatorText("Display");
        ImGui::SliderFloat("Opacity", &object.Opacity, 0.0f, 1.0f, "%.2f");

        Ref<MaterialPBR> pbr = std::dynamic_pointer_cast<MaterialPBR>(object.Meshes[0]->Mat);
        if (pbr)
        {
            ImGui::SeparatorText("PBR Material");
            ImGui::ColorEdit3("Albedo", &pbr->Albedo.x);
            ImGui::SliderFloat("Metallic", &pbr->Metallic, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Roughness", &pbr->Roughness, 0.04f, 1.0f, "%.2f");
            ImGui::SliderFloat("Material AO", &pbr->AmbientOcclusion, 0.0f, 1.0f, "%.2f");
        }

        Ref<Mesh> edgeMesh;
        for (const auto& mesh : object.Meshes)
        {
            if (mesh && !mesh->EdgeVertices.empty())
            {
                edgeMesh = mesh;
                break;
            }
        }
        if (edgeMesh)
        {
            ImGui::SeparatorText("STEP Display");
            const bool showEdges = edgeMesh->ShowEdges;
            if (ImGui::Button(showEdges ? "Hide Edges" : "Show Edges"))
            {
                for (const auto& mesh : object.Meshes)
                {
                    if (mesh && !mesh->EdgeVertices.empty())
                        mesh->ShowEdges = !showEdges;
                }
            }
        }

        Ref<ToonMaterial> toon = std::dynamic_pointer_cast<ToonMaterial>(object.Meshes[0]->Mat);
        if (toon)
        {
            ImGui::SeparatorText("Toon Material");
            ImGui::ColorEdit3("Diffuse", &toon->Diffuse.x);
            ImGui::ColorEdit3("Ambient", &toon->Ambient.x);
            ImGui::ColorEdit3("Specular", &toon->Specular.x);
            ImGui::SliderFloat("Shininess", &toon->SpecularPower, 1.0f, 128.0f, "%.1f");
            ImGui::SliderFloat("Alpha", &toon->Alpha, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("Two Sided", &toon->TwoSided);
            ImGui::Checkbox("Edge", &toon->EdgeEnabled);
            if (toon->EdgeEnabled)
            {
                ImGui::ColorEdit4("Edge Color", &toon->EdgeColor.x);
                float edgeSize = toon->EdgeSize;
                if (ImGui::SliderFloat("Edge Size", &edgeSize, 0.0f, 8.0f, "%.2f px"))
                {
                    for (const auto& mesh : object.Meshes)
                    {
                        Ref<ToonMaterial> meshToon = std::dynamic_pointer_cast<ToonMaterial>(mesh->Mat);
                        if (meshToon)
                            meshToon->EdgeSize = edgeSize;
                    }
                }
            }
        }
    }
};

class GCodeInspector : public ObjectInspector
{
public:
    bool CanInspect(const Object3D& object) const override
    {
        return dynamic_cast<const GCodeObject*>(&object) != nullptr;
    }

    void Draw(Object3D& object) override
    {
        auto& gcodeObject = static_cast<GCodeObject&>(object);
        ImGui::SeparatorText("GCode");
        ImGui::TextDisabled("%s", gcodeObject.GetSourceName().c_str());

        float progress = gcodeObject.GetProgress();
        if (ImGui::SliderFloat("Progress", &progress, 0.0f, 1.0f, "%.3f"))
            gcodeObject.SetProgress(progress);
        ImGui::TextDisabled("Line %d / %d | %.2f mm",
            gcodeObject.GetDisplayIndex(),
            gcodeObject.GetTotalSegmentCount(),
            gcodeObject.GetTotalDistance());

        if (ImGui::Button(gcodeObject.IsPlaying() ? "Pause" : "Play"))
            gcodeObject.TogglePlaying();
        ImGui::SameLine();
        if (ImGui::Button("Reset##GCode"))
            gcodeObject.Reset();

        bool showFastMoves = gcodeObject.ShowFastMoves();
        if (ImGui::Checkbox("Show Fast Moves", &showFastMoves))
            gcodeObject.ShowFastMoves() = showFastMoves;
        bool showTool = gcodeObject.ShowTool();
        if (ImGui::Checkbox("Show Tool", &showTool))
            gcodeObject.ShowTool() = showTool;
        float speed = gcodeObject.PlaybackSpeed();
        if (ImGui::SliderFloat("Playback Speed", &speed, 1.0f, 1000.0f, "%.0f seg/s"))
            gcodeObject.PlaybackSpeed() = speed;
    }
};

class TerrainCDLODInspector : public ObjectInspector
{
public:
    bool CanInspect(const Object3D& object) const override
    {
        return dynamic_cast<const TerrainCDLOD*>(&object) != nullptr;
    }

    void Draw(Object3D& object) override
    {
        auto& terrain = static_cast<TerrainCDLOD&>(object);
        const TerrainCDLOD::Config& config = terrain.GetConfig();
        const glm::ivec2 heightmapSize = terrain.GetHeightmapSize();

        ImGui::SeparatorText("CDLOD Terrain");
        ImGui::TextDisabled("Loaded: %s", terrain.IsLoaded() ? "Yes" : "No");
        ImGui::Text("Heightmap: %d x %d", heightmapSize.x, heightmapSize.y);
        ImGui::Text("Selected nodes: %zu", terrain.GetSelectedNodeCount());
        ImGui::Text("Leaf size: %d", config.LeafQuadTreeNodeSize);
        ImGui::Text("Grid multiplier: %d", config.RenderGridResolutionMult);
        ImGui::Text("LOD levels: %d", config.LODLevelCount);
        ImGui::Text("LOD distance ratio: %.2f", config.LODLevelDistanceRatio);
        ImGui::Text("View range: %.0f - %.0f", config.MinViewRange, config.MaxViewRange);
        ImGui::Text("Map min: %.1f, %.1f, %.1f",
            config.MapDims.MinX, config.MapDims.MinY, config.MapDims.MinZ);
        ImGui::Text("Map size: %.1f, %.1f, %.1f",
            config.MapDims.SizeX, config.MapDims.SizeY, config.MapDims.SizeZ);
    }
};

class VolumeObjectInspector : public ObjectInspector
{
public:
    bool CanInspect(const Object3D& object) const override
    {
        return dynamic_cast<const VolumeObject*>(&object) != nullptr;
    }

    void Draw(Object3D& object) override
    {
        auto& volume = static_cast<VolumeObject&>(object);
        const glm::uvec3 dimensions = volume.GetDimensions();

        ImGui::SeparatorText("Volume");
        ImGui::TextDisabled("Loaded: %s", volume.IsLoaded() ? "Yes" : "No");
        ImGui::Text("Dimensions: %u x %u x %u", dimensions.x, dimensions.y, dimensions.z);

        int stepCount = volume.StepCount();
        if (ImGui::SliderInt("Step Count", &stepCount, 16, 1024))
            volume.StepCount() = stepCount;
        ImGui::SliderFloat("Density", &volume.Density(), 0.1f, 100.0f, "%.2f");
        ImGui::SliderFloat("Exposure", &volume.Exposure(), 0.1f, 40.0f, "%.2f");
    }
};
}

void ObjectInspectorRegistry::Register(std::unique_ptr<ObjectInspector> inspector)
{
    if (inspector)
        Inspectors().push_back(std::move(inspector));
}

void ObjectInspectorRegistry::DrawInspector(Object3D& object)
{
    EnsureDefaultInspectors();
    for (const auto& inspector : Inspectors())
    {
        if (inspector->CanInspect(object))
            inspector->Draw(object);
    }
}

std::vector<std::unique_ptr<ObjectInspector>>& ObjectInspectorRegistry::Inspectors()
{
    static std::vector<std::unique_ptr<ObjectInspector>> inspectors;
    return inspectors;
}

void ObjectInspectorRegistry::EnsureDefaultInspectors()
{
    static bool registered = false;
    if (registered)
        return;

    Register(std::make_unique<GCodeInspector>());
    Register(std::make_unique<TerrainCDLODInspector>());
    Register(std::make_unique<VolumeObjectInspector>());
    Register(std::make_unique<MeshDisplayInspector>());
    registered = true;
}
