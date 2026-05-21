#pragma once

#include "Object3D.h"
#include <string>
#include <vector>

// Primitive objects that inherit from Object3D for direct scene management
class SceneCube : public Object3D
{
public:
    SceneCube(float size = 1.0f);
};

class SceneSphere : public Object3D
{
public:
    SceneSphere(float radius = 1.0f, uint32_t sectorCount = 24, uint32_t stackCount = 16);
};

class ScenePlane : public Object3D
{
public:
    ScenePlane(float size = 1.0f);
};

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

    // Primitive factories — creates and adds to scene
    Ref<SceneCube> CreateCube(const std::string& name = "Cube");
    Ref<SceneSphere> CreateSphere(const std::string& name = "Sphere");
    Ref<ScenePlane> CreatePlane(const std::string& name = "Plane", float size = 100.0f);

    int GetSelectedIndex() const { return m_SelectedIndex; }
    void SetSelectedIndex(int index);
    Entry* GetSelectedEntry();
    Transform* GetSelectedTransform();

private:
    int AddObjectRaw(const Ref<Object3D>& object, const std::string& name, const std::string& filepath);
    std::vector<Entry> m_Objects;
    int m_SelectedIndex = -1;
};
