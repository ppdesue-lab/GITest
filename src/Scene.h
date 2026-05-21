#pragma once

#include "Object3D.h"
#include <string>
#include <vector>

class Scene
{
public:
    struct Entry {
        Ref<Object3D> Object;
        std::string Name;
        std::string FilePath;
        bool Visible = true;
    };

    Scene() = default;
    ~Scene() = default;

    int AddObject(const Ref<Object3D>& object, const std::string& name = "", const std::string& filepath = "");
    void RemoveObject(int index);
    void Clear();

    Entry* GetEntry(int index);
    const std::vector<Entry>& GetObjects() const { return m_Objects; }
    int GetCount() const { return (int)m_Objects.size(); }

    int GetSelectedIndex() const { return m_SelectedIndex; }
    void SetSelectedIndex(int index);
    Entry* GetSelectedEntry();
    Transform* GetSelectedTransform();

private:
    std::vector<Entry> m_Objects;
    int m_SelectedIndex = -1;
};
