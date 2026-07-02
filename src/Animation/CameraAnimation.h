#pragma once

#include "base.h"

#include <Camera/Camera.h>
#include <tanim/tanim.hpp>

#include <entt/entt.hpp>
#include <filesystem>
#include <string>
#include <vector>

struct CameraPose
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 target = glm::vec3(0.0f);
    float fov = 45.0f;
};

class CameraAnimation
{
public:
    void Initialize();
    void Clear();
    void Update(const Ref<Camera>& camera, float deltaTime);
    void OnImGuiRender(const Ref<Camera>& camera, float deltaTime);

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible);

    bool HasTimeline() const { return m_HasTimeline; }
    void EnsureTimeline(const Ref<Camera>& camera);
    void OpenEditor(const Ref<Camera>& camera);
    void RecordCurrentCamera(const Ref<Camera>& camera);

    void Play(const Ref<Camera>& camera);
    void Pause();
    void Stop(const Ref<Camera>& camera);
    bool IsPlaying() const { return tanim::IsPlaying(m_Component); }

    bool Save(const std::filesystem::path& filepath);
    bool Load(const Ref<Camera>& camera, const std::filesystem::path& filepath);
    const std::filesystem::path& GetTimelinePath() const { return m_FilePath; }

private:
    void EnsureCameraPoseComponent();
    void EnsureCameraPoseSequences();
    void SyncCameraToRegistry(const Ref<Camera>& camera);
    void SyncRegistryToCamera(const Ref<Camera>& camera);
    void RecordCurrentFrameKeyframes();
    void EnsureZeroFrameKeyframes();
    CameraPose ReadCameraPose(const Ref<Camera>& camera) const;
    void ApplyCameraPose(const Ref<Camera>& camera, const CameraPose& pose);

    tanim::TimelineData m_Timeline;
    tanim::ComponentData m_Component;
    std::vector<tanim::EntityData> m_EntityData;
    entt::registry m_Registry;
    entt::entity m_Entity = entt::null;
    std::filesystem::path m_FilePath;
    float m_TargetDistance = 100.0f;
    bool m_Initialized = false;
    bool m_HasTimeline = false;
    bool m_Started = false;
    bool m_Visible = false;
    bool m_InputWasEnabled = false;
};
