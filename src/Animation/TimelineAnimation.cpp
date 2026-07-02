#include "Animation/TimelineAnimation.h"

#include "Log.h"

#include <tanim/registry.hpp>
#include <tanim/sequence_id.hpp>
#include <tanim/tanim_internal.hpp>
#include <tanim/timeline.hpp>

#include <fstream>
#include <sstream>
#include <unordered_set>

VISITABLE_STRUCT(Transform, translation, rotation, scale);
TANIM_REFLECT(Transform, translation, rotation, scale);

namespace
{
struct TanimObjectUserData
{
    entt::entity Entity = entt::null;
    std::string UID;
};
}

entt::entity tanim::FindEntityOfUID(const ComponentData& cdata, const std::string& uid_to_find)
{
    const auto* userData = std::any_cast<TanimObjectUserData>(&cdata.m_user_data);
    if (!userData)
    {
        LogError("missing object user data for uid " + uid_to_find);
        return entt::null;
    }
    return userData->UID == uid_to_find ? userData->Entity : entt::null;
}

void tanim::LogError(const std::string& message)
{
    ERROR("[TANIM] {}", message);
}

void tanim::LogInfo(const std::string& message)
{
    INFO("[TANIM] {}", message);
}

void TimelineAnimation::Initialize()
{
    if (m_Initialized)
        return;
    tanim::Init();
    m_Initialized = true;
}

void TimelineAnimation::Clear()
{
    if (m_Initialized)
    {
        tanim::CloseEditor();
        StopAll();
    }
    m_Tracks.clear();
    m_Registry.clear();
    m_Visible = false;
}

void TimelineAnimation::Update(Scene& scene, float deltaTime)
{
    if (!m_Initialized)
        return;

    RemoveTracksMissingFrom(scene);

    for (int i = 0; i < scene.GetCount(); i++)
    {
        Scene::Entry* entry = scene.GetEntry(i);
        if (!entry || !entry->Object)
            continue;

        Track* track = FindTrack(*entry);
        if (!track)
            continue;

        if (track->Started && tanim::IsPlaying(track->Component))
        {
            tanim::UpdateTimeline(m_Registry, track->EntityData, track->Timeline, track->Component, deltaTime);
            SyncRegistryToObject(*entry, *track);
        }
        else if (tanim::IsPlaying(track->Component))
        {
            SyncRegistryToObject(*entry, *track);
        }
        else
        {
            SyncObjectToRegistry(*entry, *track);
        }
    }
}

void TimelineAnimation::OnImGuiRender(Scene& scene, float deltaTime)
{
    if (!m_Initialized)
        return;

    if (m_Visible)
    {
        OpenSelectedTimeline(scene);
        Scene::Entry* selectedEntry = scene.GetSelectedCount() == 1 ? scene.GetEntry(scene.GetSelectedIndex()) : nullptr;
        Track* selectedTrack = selectedEntry ? FindTrack(*selectedEntry) : nullptr;
        Transform registryBeforeTanim;
        bool hasRegistryBeforeTanim = false;
        if (selectedTrack && selectedTrack->Entity != entt::null && m_Registry.valid(selectedTrack->Entity) &&
            m_Registry.all_of<Transform>(selectedTrack->Entity))
        {
            registryBeforeTanim = m_Registry.get<Transform>(selectedTrack->Entity);
            hasRegistryBeforeTanim = true;
        }

        tanim::UpdateEditor(deltaTime);
        tanim::Draw();
        const bool timelineSampled = tanim::ConsumeEditorTimelineSampled();
        if (selectedEntry && selectedTrack && selectedTrack->Entity != entt::null &&
            m_Registry.valid(selectedTrack->Entity) && m_Registry.all_of<Transform>(selectedTrack->Entity))
        {
            const Transform& registryAfterTanim = m_Registry.get<Transform>(selectedTrack->Entity);
            const bool tanimChangedTransform =
                !hasRegistryBeforeTanim || !IsSameTransform(registryBeforeTanim, registryAfterTanim);
            if ((timelineSampled || tanim::IsPlaying(selectedTrack->Component)) && tanimChangedTransform)
                SyncRegistryToObject(*selectedEntry, *selectedTrack);
        }
    }
    else
    {
        tanim::UpdateEditor(deltaTime);
    }
}

void TimelineAnimation::SetVisible(bool visible)
{
    m_Visible = visible;
    if (!m_Visible && m_Initialized)
        tanim::CloseEditor();
}

bool TimelineAnimation::HasTimeline(const Scene::Entry& entry) const
{
    return FindTrack(entry) != nullptr;
}

void TimelineAnimation::EnsureTimeline(Scene::Entry& entry, int sceneIndex)
{
    EnsureTrack(entry, sceneIndex);
}

void TimelineAnimation::OpenEditor(Scene::Entry& entry, int sceneIndex)
{
    Track& track = EnsureTrack(entry, sceneIndex);
    EnsureTransformSequences(track);
    SyncObjectToRegistry(entry, track);
    tanim::OpenForEditing(m_Registry, track.EntityData, track.Timeline, track.Component);
    m_Visible = true;
}

void TimelineAnimation::Play(Scene::Entry& entry, int sceneIndex)
{
    Track& track = EnsureTrack(entry, sceneIndex);
    SyncObjectToRegistry(entry, track);
    if (!m_PlayMode)
    {
        tanim::EnterPlayMode();
        m_PlayMode = true;
    }
    tanim::StartTimeline(track.Timeline, track.Component);
    tanim::Play(track.Component);
    track.Started = true;
}

void TimelineAnimation::Pause(Scene::Entry& entry)
{
    if (Track* track = FindTrack(entry))
        tanim::Pause(track->Component);
}

void TimelineAnimation::Stop(Scene::Entry& entry)
{
    if (Track* track = FindTrack(entry))
    {
        tanim::Stop(track->Component);
        track->Started = false;
        SyncRegistryToObject(entry, *track);
    }
}

void TimelineAnimation::PlayAll(Scene& scene)
{
    for (int i = 0; i < scene.GetCount(); i++)
    {
        Scene::Entry* entry = scene.GetEntry(i);
        if (entry && FindTrack(*entry))
            Play(*entry, i);
    }
}

void TimelineAnimation::StopAll()
{
    for (auto& [object, track] : m_Tracks)
    {
        tanim::Stop(track.Component);
        track.Started = false;
    }
    if (m_PlayMode)
    {
        tanim::ExitPlayMode();
        m_PlayMode = false;
    }
}

bool TimelineAnimation::IsPlaying(const Scene::Entry& entry) const
{
    const Track* track = FindTrack(entry);
    return track && tanim::IsPlaying(track->Component);
}

bool TimelineAnimation::Save(Scene::Entry& entry, const std::filesystem::path& filepath)
{
    Track* track = FindTrack(entry);
    if (!track)
        return false;

    std::ofstream output(filepath, std::ios::binary);
    if (!output)
        return false;

    const std::string serialized = tanim::Serialize(track->Timeline);
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    track->FilePath = filepath;
    track->Timeline.m_name = filepath.stem().u8string();
    return true;
}

bool TimelineAnimation::Load(Scene::Entry& entry, int sceneIndex, const std::filesystem::path& filepath)
{
    std::ifstream input(filepath, std::ios::binary);
    if (!input)
        return false;

    std::stringstream buffer;
    buffer << input.rdbuf();

    Track& track = EnsureTrack(entry, sceneIndex);
    tanim::Deserialize(track.Timeline, buffer.str());
    track.FilePath = filepath;
    track.Timeline.m_name = filepath.stem().u8string();
    SyncObjectToRegistry(entry, track);
    return true;
}

std::string TimelineAnimation::GetTimelineName(const Scene::Entry& entry) const
{
    const Track* track = FindTrack(entry);
    return track ? track->Timeline.m_name : std::string();
}

std::filesystem::path TimelineAnimation::GetTimelinePath(const Scene::Entry& entry) const
{
    const Track* track = FindTrack(entry);
    return track ? track->FilePath : std::filesystem::path();
}

void TimelineAnimation::RecordSelectedTransformChange(Scene& scene)
{
    if (scene.GetSelectedCount() != 1)
        return;

    const int selectedIndex = scene.GetSelectedIndex();
    Scene::Entry* entry = scene.GetEntry(selectedIndex);
    if (!entry || !entry->Object)
        return;

    Track* track = FindTrack(*entry);
    if (!track || tanim::IsPlaying(track->Component))
        return;

    EnsureTransformSequences(*track);
    if (SyncObjectToRegistry(*entry, *track))
        RecordCurrentFrameKeyframes(*track);
}

TimelineAnimation::Track& TimelineAnimation::EnsureTrack(Scene::Entry& entry, int sceneIndex)
{
    Object3D* object = entry.Object.get();
    Track& track = m_Tracks[object];
    track.UID = MakeUID(entry, sceneIndex);
    track.EntityData = { { track.UID, entry.Name.empty() ? track.UID : entry.Name } };

    if (track.Entity == entt::null || !m_Registry.valid(track.Entity))
        track.Entity = m_Registry.create();

    track.Component.m_user_data = TanimObjectUserData{ track.Entity, track.UID };
    SyncObjectToRegistry(entry, track);
    return track;
}

TimelineAnimation::Track* TimelineAnimation::FindTrack(const Scene::Entry& entry)
{
    if (!entry.Object)
        return nullptr;
    auto it = m_Tracks.find(entry.Object.get());
    return it == m_Tracks.end() ? nullptr : &it->second;
}

const TimelineAnimation::Track* TimelineAnimation::FindTrack(const Scene::Entry& entry) const
{
    if (!entry.Object)
        return nullptr;
    auto it = m_Tracks.find(entry.Object.get());
    return it == m_Tracks.end() ? nullptr : &it->second;
}

bool TimelineAnimation::SyncObjectToRegistry(Scene::Entry& entry, Track& track)
{
    Transform* transform = GetAnimatableTransform(entry);
    if (!transform || track.Entity == entt::null || !m_Registry.valid(track.Entity))
        return false;

    bool changed = true;
    if (m_Registry.all_of<Transform>(track.Entity))
    {
        changed = !IsSameTransform(m_Registry.get<Transform>(track.Entity), *transform);
        m_Registry.get<Transform>(track.Entity) = *transform;
    }
    else
    {
        m_Registry.emplace<Transform>(track.Entity, *transform);
    }

    return changed;
}

void TimelineAnimation::SyncRegistryToObject(Scene::Entry& entry, Track& track)
{
    Transform* transform = GetAnimatableTransform(entry);
    if (!transform || track.Entity == entt::null || !m_Registry.valid(track.Entity) ||
        !m_Registry.all_of<Transform>(track.Entity))
        return;

    *transform = m_Registry.get<Transform>(track.Entity);
    if (entry.Object)
        entry.Object->UpdateBoundingSphere();
}

void TimelineAnimation::OpenSelectedTimeline(Scene& scene)
{
    if (scene.GetSelectedCount() != 1)
    {
        tanim::CloseEditor();
        return;
    }

    const int selectedIndex = scene.GetSelectedIndex();
    Scene::Entry* entry = scene.GetEntry(selectedIndex);
    if (!entry || !entry->Object)
    {
        tanim::CloseEditor();
        return;
    }

    Track& track = EnsureTrack(*entry, selectedIndex);
    EnsureTransformSequences(track);
    if (!tanim::IsPlaying(track.Component))
        SyncObjectToRegistry(*entry, track);
    tanim::OpenForEditing(m_Registry, track.EntityData, track.Timeline, track.Component);
}

void TimelineAnimation::EnsureTransformSequences(Track& track)
{
    if (track.EntityData.empty())
        return;

    const char* fields[] = { "translation", "rotation", "scale" };
    for (const auto& component : tanim::internal::GetRegistry().GetComponents())
    {
        if (component.m_struct_name != "Transform")
            continue;

        for (const char* field : fields)
        {
            const std::string fullName =
                tanim::helpers::MakeFullName(track.EntityData[0].m_uid, component.m_struct_name, field);
            if (tanim::internal::Timeline::HasSequenceWithFullName(track.Timeline, fullName))
                continue;

            tanim::internal::SequenceId sequenceId(track.EntityData[0], component.m_struct_name, field);
            component.m_add_sequence(m_Registry, track.Timeline, track.Component, sequenceId);
        }
        EnsureZeroFrameKeyframes(track);
        return;
    }
}

void TimelineAnimation::RemoveTracksMissingFrom(Scene& scene)
{
    std::unordered_set<Object3D*> liveObjects;
    for (const auto& entry : scene.GetObjects())
    {
        if (entry.Object)
            liveObjects.insert(entry.Object.get());
    }

    for (auto it = m_Tracks.begin(); it != m_Tracks.end();)
    {
        if (liveObjects.find(it->first) == liveObjects.end())
        {
            tanim::CloseEditor();
            if (it->second.Entity != entt::null && m_Registry.valid(it->second.Entity))
                m_Registry.destroy(it->second.Entity);
            it = m_Tracks.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TimelineAnimation::RecordCurrentFrameKeyframes(Track& track)
{
    if (track.Entity == entt::null || !m_Registry.valid(track.Entity) || !m_Registry.all_of<Transform>(track.Entity))
        return;

    const int currentFrame = tanim::internal::Timeline::GetPlayerFrame(track.Timeline, track.Component);
    for (tanim::internal::Sequence& sequence : track.Timeline.m_sequences)
    {
        const bool hasKeyframe = sequence.IsKeyframeInAnyCurve(currentFrame);
        if (!hasKeyframe)
        {
            if (currentFrame == 0 && !sequence.HasAnyKeyframe())
                continue;
            sequence.AddNewKeyframe(currentFrame);
        }
        else if (!sequence.IsKeyframeInAllCurves(currentFrame))
        {
            sequence.AddNewKeyframe(currentFrame);
        }

        tanim::internal::RecordSequenceKeyframe(m_Registry, track.EntityData, track.Component, sequence, currentFrame);
    }
    EnsureZeroFrameKeyframes(track);
}

void TimelineAnimation::EnsureZeroFrameKeyframes(Track& track)
{
    for (tanim::internal::Sequence& sequence : track.Timeline.m_sequences)
        sequence.EnsureZeroFrameKeyframe();
}

Transform* TimelineAnimation::GetAnimatableTransform(Scene::Entry& entry)
{
    if (!entry.Object)
        return nullptr;
    if (entry.Object->Meshes.size() == 1 && entry.Object->Meshes[0] && entry.Object->Meshes[0]->Mat)
        return &entry.Object->Meshes[0]->Transfm;
    return &entry.Object->Transfm;
}

bool TimelineAnimation::IsSameTransform(const Transform& a, const Transform& b)
{
    constexpr float epsilon = 0.00001f;
    return glm::length(a.translation - b.translation) <= epsilon &&
           glm::length(a.scale - b.scale) <= epsilon &&
           std::abs(glm::abs(glm::dot(glm::normalize(a.rotation), glm::normalize(b.rotation))) - 1.0f) <= epsilon;
}

std::string TimelineAnimation::MakeUID(const Scene::Entry& entry, int sceneIndex)
{
    return std::to_string(sceneIndex) + ":" + (entry.Name.empty() ? "Object" : entry.Name);
}
