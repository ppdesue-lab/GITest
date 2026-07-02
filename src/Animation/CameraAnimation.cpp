#include "Animation/CameraAnimation.h"

#include "Animation/TanimEntityBinding.h"
#include "Camera/FPSCamera.h"
#include "Log.h"

#include <tanim/registry.hpp>
#include <tanim/sequence_id.hpp>
#include <tanim/tanim_internal.hpp>
#include <tanim/timeline.hpp>

#include <imgui.h>

#ifdef ERROR
#undef ERROR
#endif
#include <thirdparty/ofd/portable-file-dialogs.h>
#ifdef ERROR
#undef ERROR
#endif

#include <fstream>
#include <sstream>

VISITABLE_STRUCT(CameraPose, position, target, fov);
TANIM_REFLECT(CameraPose, position, target, fov);

void CameraAnimation::Initialize()
{
    if (m_Initialized)
        return;

    tanim::Init();
    EnsureCameraPoseComponent();
    m_Initialized = true;
}

void CameraAnimation::Clear()
{
    if (m_Initialized)
    {
        tanim::CloseEditor();
        tanim::Stop(m_Component);
    }
    m_Registry.clear();
    m_Entity = entt::null;
    m_EntityData.clear();
    m_Timeline = tanim::TimelineData{};
    m_Component = tanim::ComponentData{};
    m_FilePath.clear();
    m_HasTimeline = false;
    m_Started = false;
    m_Visible = false;
}

void CameraAnimation::Update(const Ref<Camera>& camera, float deltaTime)
{
    if (!m_Initialized || !m_HasTimeline)
        return;

    if (m_Started && tanim::IsPlaying(m_Component))
    {
        tanim::UpdateTimeline(m_Registry, m_EntityData, m_Timeline, m_Component, deltaTime);
        SyncRegistryToCamera(camera);
    }
}

void CameraAnimation::OnImGuiRender(const Ref<Camera>& camera, float deltaTime)
{
    if (!m_Initialized)
        return;

    if (!m_Visible)
    {
        tanim::SetTimelinePanelHeaderDrawCallback({});
        return;
    }

    EnsureTimeline(camera);
    tanim::OpenForEditing(m_Registry, m_EntityData, m_Timeline, m_Component);
    tanim::SetTimelinePanelHeaderDrawCallback(
        [this, camera]()
        {
            ImGui::TextDisabled("%s", m_Timeline.m_name.empty() ? "Camera Timeline" : m_Timeline.m_name.c_str());
            ImGui::DragFloat("Target Distance", &m_TargetDistance, 1.0f, 1.0f, 100000.0f);

            if (ImGui::Button("Record Current Camera"))
                RecordCurrentCamera(camera);

            ImGui::SameLine();
            if (tanim::IsPlaying(m_Component))
            {
                if (ImGui::Button("Pause"))
                    Pause();
            }
            else
            {
                if (ImGui::Button("Play"))
                    Play(camera);
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop"))
                Stop(camera);

            if (ImGui::Button("Load .tanim"))
            {
                auto files =
                    pfd::open_file("Load Camera Timeline", "", { "Tanim Timeline", "*.tanim", "All Files", "*" }).result();
                if (!files.empty() && !Load(camera, std::filesystem::u8path(files[0])))
                    ::Log::GetCoreLogger()->error("Failed to load camera timeline: {}", files[0]);
            }

            ImGui::SameLine();
            if (ImGui::Button("Save .tanim"))
            {
                std::string defaultPath;
                if (!m_FilePath.empty())
                    defaultPath = m_FilePath.u8string();

                auto filepath = pfd::save_file(
                                    "Save Camera Timeline",
                                    defaultPath,
                                    { "Tanim Timeline", "*.tanim", "All Files", "*" })
                                    .result();
                if (!filepath.empty())
                {
                    std::filesystem::path path = std::filesystem::u8path(filepath);
                    if (path.extension().empty())
                        path += ".tanim";
                    if (!Save(path))
                        ::Log::GetCoreLogger()->error("Failed to save camera timeline: {}", path.u8string());
                }
            }
        });

    if (!(m_Started && tanim::IsPlaying(m_Component)))
        tanim::UpdateEditor(deltaTime);
    tanim::Draw();
    tanim::SetTimelinePanelHeaderDrawCallback({});
    const bool timelineSampled = tanim::ConsumeEditorTimelineSampled();
    if (timelineSampled || tanim::IsPlaying(m_Component))
        SyncRegistryToCamera(camera);
}

void CameraAnimation::SetVisible(bool visible)
{
    m_Visible = visible;
    if (!m_Visible && m_Initialized)
    {
        tanim::SetTimelinePanelHeaderDrawCallback({});
        tanim::CloseEditor();
    }
}

void CameraAnimation::EnsureTimeline(const Ref<Camera>& camera)
{
    EnsureCameraPoseComponent();

    if (m_Entity == entt::null || !m_Registry.valid(m_Entity))
        m_Entity = m_Registry.create();

    m_EntityData = { { "camera:main", "Camera" } };
    m_Component.m_user_data = TanimEntityBinding{ m_Entity, m_EntityData[0].m_uid };
    if (m_Timeline.m_name.empty() || m_Timeline.m_name == "New Timeline")
        m_Timeline.m_name = "Camera Timeline";

    SyncCameraToRegistry(camera);
    EnsureCameraPoseSequences();
    m_HasTimeline = true;
}

void CameraAnimation::OpenEditor(const Ref<Camera>& camera)
{
    EnsureTimeline(camera);
    tanim::OpenForEditing(m_Registry, m_EntityData, m_Timeline, m_Component);
    m_Visible = true;
}

void CameraAnimation::RecordCurrentCamera(const Ref<Camera>& camera)
{
    EnsureTimeline(camera);
    SyncCameraToRegistry(camera);
    RecordCurrentFrameKeyframes();
}

void CameraAnimation::Play(const Ref<Camera>& camera)
{
    EnsureTimeline(camera);
    SyncCameraToRegistry(camera);
    tanim::StartTimeline(m_Timeline, m_Component);
    tanim::Play(m_Component);
    m_Started = true;
    if (camera)
    {
        m_InputWasEnabled = camera->isInputEnabled();
        camera->setInputEnabled(false);
    }
}

void CameraAnimation::Pause()
{
    tanim::Pause(m_Component);
}

void CameraAnimation::Stop(const Ref<Camera>& camera)
{
    tanim::Stop(m_Component);
    m_Started = false;
    if (camera)
        camera->setInputEnabled(m_InputWasEnabled);
}

bool CameraAnimation::Save(const std::filesystem::path& filepath)
{
    if (!m_HasTimeline)
        return false;

    std::ofstream output(filepath, std::ios::binary);
    if (!output)
        return false;

    const std::string serialized = tanim::Serialize(m_Timeline);
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    m_FilePath = filepath;
    m_Timeline.m_name = filepath.stem().u8string();
    return true;
}

bool CameraAnimation::Load(const Ref<Camera>& camera, const std::filesystem::path& filepath)
{
    std::ifstream input(filepath, std::ios::binary);
    if (!input)
        return false;

    std::stringstream buffer;
    buffer << input.rdbuf();

    EnsureTimeline(camera);
    tanim::Deserialize(m_Timeline, buffer.str());
    m_FilePath = filepath;
    m_Timeline.m_name = filepath.stem().u8string();
    SyncCameraToRegistry(camera);
    EnsureCameraPoseSequences();
    return true;
}

void CameraAnimation::EnsureCameraPoseComponent()
{
    if (m_Entity != entt::null && m_Registry.valid(m_Entity) && m_Registry.all_of<CameraPose>(m_Entity))
        return;

    if (m_Entity == entt::null || !m_Registry.valid(m_Entity))
        m_Entity = m_Registry.create();

    if (!m_Registry.all_of<CameraPose>(m_Entity))
        m_Registry.emplace<CameraPose>(m_Entity);
}

void CameraAnimation::EnsureCameraPoseSequences()
{
    if (m_EntityData.empty())
        return;

    const char* fields[] = { "position", "target", "fov" };
    for (const auto& component : tanim::internal::GetRegistry().GetComponents())
    {
        if (component.m_struct_name != "CameraPose")
            continue;

        for (const char* field : fields)
        {
            const std::string fullName =
                tanim::helpers::MakeFullName(m_EntityData[0].m_uid, component.m_struct_name, field);
            if (tanim::internal::Timeline::HasSequenceWithFullName(m_Timeline, fullName))
                continue;

            tanim::internal::SequenceId sequenceId(m_EntityData[0], component.m_struct_name, field);
            component.m_add_sequence(m_Registry, m_Timeline, m_Component, sequenceId);
        }
        EnsureZeroFrameKeyframes();
        return;
    }
}

void CameraAnimation::SyncCameraToRegistry(const Ref<Camera>& camera)
{
    EnsureCameraPoseComponent();
    if (m_Entity == entt::null || !m_Registry.valid(m_Entity) || !m_Registry.all_of<CameraPose>(m_Entity))
        return;

    m_Registry.get<CameraPose>(m_Entity) = ReadCameraPose(camera);
}

void CameraAnimation::SyncRegistryToCamera(const Ref<Camera>& camera)
{
    if (m_Entity == entt::null || !m_Registry.valid(m_Entity) || !m_Registry.all_of<CameraPose>(m_Entity))
        return;

    ApplyCameraPose(camera, m_Registry.get<CameraPose>(m_Entity));
}

void CameraAnimation::RecordCurrentFrameKeyframes()
{
    if (m_Entity == entt::null || !m_Registry.valid(m_Entity) || !m_Registry.all_of<CameraPose>(m_Entity))
        return;

    const int currentFrame = tanim::internal::Timeline::GetPlayerFrame(m_Timeline, m_Component);
    for (tanim::internal::Sequence& sequence : m_Timeline.m_sequences)
    {
        const bool hasKeyframe = sequence.IsKeyframeInAnyCurve(currentFrame);
        if (!hasKeyframe)
        {
            sequence.AddNewKeyframe(currentFrame);
        }
        else if (!sequence.IsKeyframeInAllCurves(currentFrame))
        {
            sequence.AddNewKeyframe(currentFrame);
        }

        tanim::internal::RecordSequenceKeyframe(m_Registry, m_EntityData, m_Component, sequence, currentFrame);
    }
    EnsureZeroFrameKeyframes();
}

void CameraAnimation::EnsureZeroFrameKeyframes()
{
    for (tanim::internal::Sequence& sequence : m_Timeline.m_sequences)
        sequence.EnsureZeroFrameKeyframe();
}

CameraPose CameraAnimation::ReadCameraPose(const Ref<Camera>& camera) const
{
    CameraPose pose;
    const Ref<FPSCamera> fpsCamera = std::dynamic_pointer_cast<FPSCamera>(camera);
    if (!fpsCamera)
        return pose;

    pose.position = fpsCamera->getPosition();
    pose.target = fpsCamera->getPosition() + fpsCamera->getForward() * m_TargetDistance;
    pose.fov = fpsCamera->getFOV();
    return pose;
}

void CameraAnimation::ApplyCameraPose(const Ref<Camera>& camera, const CameraPose& pose)
{
    const Ref<FPSCamera> fpsCamera = std::dynamic_pointer_cast<FPSCamera>(camera);
    if (!fpsCamera)
        return;

    fpsCamera->setPosition(pose.position);
    fpsCamera->lookAt(pose.target);
    fpsCamera->setFOV(pose.fov);
}
