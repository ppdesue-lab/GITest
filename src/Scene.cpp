#include "Scene.h"

int Scene::AddObject(const Ref<Object3D>& object, const std::string& name, const std::string& filepath)
{
    if (!object)
        return -1;

    Entry entry;
    entry.Object = object;
    entry.Name = name.empty() ? ("Object " + std::to_string(m_Objects.size())) : name;
    entry.FilePath = filepath;
    m_Objects.push_back(entry);

    // Auto-select the newly added object
    SetSelectedIndex((int)m_Objects.size() - 1);
    return (int)m_Objects.size() - 1;
}

void Scene::RemoveObject(int index)
{
    if (index < 0 || index >= (int)m_Objects.size())
        return;

    m_Objects.erase(m_Objects.begin() + index);

    if (m_SelectedIndex >= (int)m_Objects.size())
        m_SelectedIndex = (int)m_Objects.size() - 1;
}

void Scene::Clear()
{
    m_Objects.clear();
    m_SelectedIndex = -1;
}

Scene::Entry* Scene::GetEntry(int index)
{
    if (index < 0 || index >= (int)m_Objects.size())
        return nullptr;
    return &m_Objects[index];
}

void Scene::SetSelectedIndex(int index)
{
    m_SelectedIndex = index;
}

Scene::Entry* Scene::GetSelectedEntry()
{
    return GetEntry(m_SelectedIndex);
}

Transform* Scene::GetSelectedTransform()
{
    Entry* entry = GetSelectedEntry();
    if (entry && !entry->Object->Meshes.empty())
        return &entry->Object->Meshes[0]->Transfm;
    return nullptr;
}
