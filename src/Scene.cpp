#include "Scene.h"
#include <Renderer/Material.h>
#include <Renderer/Buffer.h>
#include <Renderer/VertexArray.h>
#include <Renderer/VertexDesc.h>
#include <glm/gtc/constants.hpp>
#include <cmath>

// --- SceneCube ---
SceneCube::SceneCube(float size)
{
    auto mesh = CreateRef<Mesh>();
    float h = size * 0.5f;

    glm::vec3 positions[8] = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h},
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h}
    };
    std::vector<VertexNormal> verts;
    for (auto& p : positions)
        verts.push_back(VertexNormal(p, glm::normalize(p)));
    std::vector<uint32_t> idxs = {
        0,1,2, 2,3,0, 4,5,6, 6,7,4,
        0,1,5, 5,4,0, 2,3,7, 7,6,2,
        0,3,7, 7,4,0, 1,2,6, 6,5,1
    };

    auto va = VertexArray::Create();
    auto vb = VertexBuffer::Create((float*)&verts[0].Position.x, verts.size() * sizeof(VertexNormal));
    vb->SetLayout({ {ShaderDataType::Float3, "a_Position", false}, {ShaderDataType::Float3, "a_Normal", false} });
    va->AddVertexBuffer(vb);
    auto ib = IndexBuffer::Create((uint32_t*)idxs.data(), (uint32_t)idxs.size());
    va->SetIndexBuffer(ib);
    va->Unbind();

    mesh->VertexObject = va;
    mesh->Mat = CreateRef<MaterialPBR>();
    Meshes.push_back(mesh);
}

// --- SceneSphere ---
SceneSphere::SceneSphere(float radius, uint32_t sc, uint32_t st)
{
    auto mesh = CreateRef<Mesh>();
    std::vector<VertexNormalTexture> verts;
    std::vector<uint32_t> idxs;

    for (uint32_t i = 0; i <= st; ++i) {
        float stackAngle = glm::pi<float>() / 2.0f - i * glm::pi<float>() / (float)st;
        float xy = radius * cosf(stackAngle);
        float z = radius * sinf(stackAngle);
        for (uint32_t j = 0; j <= sc; ++j) {
            float sectorAngle = j * 2.0f * glm::pi<float>() / (float)sc;
            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);
            glm::vec3 pos(x, y, z);
            glm::vec2 uv((float)j / (float)sc, (float)i / (float)st);
            verts.push_back(VertexNormalTexture(pos, glm::normalize(pos), uv));
        }
    }
    for (uint32_t i = 0; i < st; ++i) {
        uint32_t k1 = i * (sc + 1);
        uint32_t k2 = k1 + sc + 1;
        for (uint32_t j = 0; j < sc; ++j, ++k1, ++k2) {
            if (i != 0) { idxs.push_back(k1); idxs.push_back(k2); idxs.push_back(k1 + 1); }
            if (i != st - 1) { idxs.push_back(k1 + 1); idxs.push_back(k2); idxs.push_back(k2 + 1); }
        }
    }

    auto va = VertexArray::Create();
    auto vb = VertexBuffer::Create((float*)&verts[0].Position.x, verts.size() * sizeof(VertexNormalTexture));
    vb->SetLayout({ {ShaderDataType::Float3, "a_Position", false}, {ShaderDataType::Float3, "a_Normal", false},
        {ShaderDataType::Float2, "a_TexCoord", false} });
    va->AddVertexBuffer(vb);
    auto ib = IndexBuffer::Create((uint32_t*)idxs.data(), (uint32_t)idxs.size());
    va->SetIndexBuffer(ib);
    va->Unbind();

    mesh->VertexObject = va;
    mesh->Mat = CreateRef<MaterialPBR>();
    Meshes.push_back(mesh);
}

// --- ScenePlane ---
ScenePlane::ScenePlane(float size)
{
    auto mesh = CreateRef<Mesh>();
    float h = size;
    std::vector<VertexNormal> verts = {
        {{-h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}},
        {{ h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}},
        {{ h, 0.0f,  h}, {0.0f, 1.0f, 0.0f}},
        {{-h, 0.0f,  h}, {0.0f, 1.0f, 0.0f}},
    };
    std::vector<uint32_t> idxs = {0, 1, 2, 2, 3, 0};

    auto va = VertexArray::Create();
    auto vb = VertexBuffer::Create((float*)&verts[0].Position.x, verts.size() * sizeof(VertexNormal));
    vb->SetLayout({ {ShaderDataType::Float3, "a_Position", false}, {ShaderDataType::Float3, "a_Normal", false} });
    va->AddVertexBuffer(vb);
    auto ib = IndexBuffer::Create((uint32_t*)idxs.data(), (uint32_t)idxs.size());
    va->SetIndexBuffer(ib);
    va->Unbind();

    mesh->VertexObject = va;
    mesh->Mat = CreateRef<MaterialPBR>();
    Meshes.push_back(mesh);
}

// --- Scene ---
int Scene::AddObjectRaw(const Ref<Object3D>& object, const std::string& name, const std::string& filepath)
{
    if (!object) return -1;
    Entry entry;
    entry.Object = object;
    entry.Name = name.empty() ? ("Object " + std::to_string(m_Objects.size())) : name;
    entry.FilePath = filepath;
    m_Objects.push_back(entry);
    return (int)m_Objects.size() - 1;
}

int Scene::AddObject(const Ref<Object3D>& object, const std::string& name, const std::string& filepath)
{
    int idx = AddObjectRaw(object, name, filepath);
    SetSelectedIndex(idx);
    return idx;
}

Ref<SceneCube> Scene::CreateCube(const std::string& name, float size)
{
    auto obj = CreateRef<SceneCube>();
    AddObjectRaw(obj, name, "");
    return obj;
}

Ref<SceneSphere> Scene::CreateSphere(const std::string& name, float radius, uint32_t sectorCount, uint32_t stackCount)
{
    auto obj = CreateRef<SceneSphere>(radius, sectorCount, stackCount);
    AddObjectRaw(obj, name, "");
    return obj;
}

Ref<ScenePlane> Scene::CreatePlane(const std::string& name, float size)
{
    auto obj = CreateRef<ScenePlane>(size);
    AddObjectRaw(obj, name, "");
    return obj;
}

void Scene::RemoveObject(int index)
{
    if (index < 0 || index >= (int)m_Objects.size()) return;
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
    if (index < 0 || index >= (int)m_Objects.size()) return nullptr;
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
    if (entry && entry->Object)
        return &entry->Object->Transfm;
    return nullptr;
}
