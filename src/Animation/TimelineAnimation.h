#pragma once

#include <Scene.h>
#include <tanim/tanim.hpp>

#include <entt/entt.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class TimelineAnimation
{
public:
    void Initialize();
    void Clear();
    void Update(Scene& scene, float deltaTime);
    void OnImGuiRender(Scene& scene, float deltaTime);
    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible);
    bool& Visible() { return m_Visible; }

    bool HasTimeline(const Scene::Entry& entry) const;
    void EnsureTimeline(Scene::Entry& entry, int sceneIndex);
    void OpenEditor(Scene::Entry& entry, int sceneIndex);
    void Play(Scene::Entry& entry, int sceneIndex);
    void Pause(Scene::Entry& entry);
    void Stop(Scene::Entry& entry);
    void PlayAll(Scene& scene);
    void StopAll();

    bool IsPlaying(const Scene::Entry& entry) const;
    bool Save(Scene::Entry& entry, const std::filesystem::path& filepath);
    bool Load(Scene::Entry& entry, int sceneIndex, const std::filesystem::path& filepath);
    std::string GetTimelineName(const Scene::Entry& entry) const;
    std::filesystem::path GetTimelinePath(const Scene::Entry& entry) const;
    void RecordSelectedTransformChange(Scene& scene);

private:
    struct Track
    {
        tanim::TimelineData Timeline;
        tanim::ComponentData Component;
        std::vector<tanim::EntityData> EntityData;
        entt::entity Entity = entt::null;
        std::string UID;
        std::filesystem::path FilePath;
        bool Started = false;
    };

    Track& EnsureTrack(Scene::Entry& entry, int sceneIndex);
    Track* FindTrack(const Scene::Entry& entry);
    const Track* FindTrack(const Scene::Entry& entry) const;
    bool SyncObjectToRegistry(Scene::Entry& entry, Track& track);
    void SyncRegistryToObject(Scene::Entry& entry, Track& track);
    void RecordCurrentFrameKeyframes(Track& track);
    void EnsureZeroFrameKeyframes(Track& track);
    void OpenSelectedTimeline(Scene& scene);
    void EnsureTransformSequences(Track& track);
    void RemoveTracksMissingFrom(Scene& scene);
    static Transform* GetAnimatableTransform(Scene::Entry& entry);
    static bool IsSameTransform(const Transform& a, const Transform& b);
    static std::string MakeUID(const Scene::Entry& entry, int sceneIndex);

    entt::registry m_Registry;
    std::unordered_map<Object3D*, Track> m_Tracks;
    bool m_Initialized = false;
    bool m_PlayMode = false;
    bool m_Visible = false;
};
