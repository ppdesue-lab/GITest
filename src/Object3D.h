#pragma once

#include <vector>
#include <filesystem>
#include <string>
#include <Transform.h>
#include <Renderer/Material.h>
#include <Renderer/VertexArray.h>

#include <Renderer/VertexDesc.h>

struct BoundingSphere
{
    glm::vec3 Center = glm::vec3(0.0f);
    float Radius = 0.0f;
    bool Valid = false;
};

struct Object3DElement
{
    std::string Name;
    Transform Transfm;
    GeometryHandle Geometry;
    uint32_t VertexCount = 0;
    uint32_t LineCount = 0;
    bool Visible = true;
    BoundingSphere Bounds;
};

class Mesh
{
public:
    ~Mesh();

    virtual void Draw(const glm::mat4& view, const glm::mat4 proj,
        const glm::mat4& parentTransform = glm::mat4(1.0f),
        float opacity = 1.0f, bool transparentPass = false);
    void UpdateBoundingSphere();
    
    Ref<Material> Mat;
    Transform Transfm;
    GeometryHandle VertexObject;
    GeometryHandle EdgeVertexObject;
    uint32_t EdgeVertexCount = 0;
    std::vector<VertexNormalTexture> TraceVertices;
    std::vector<uint32_t> TraceIndices;
    std::vector<glm::vec3> EdgeVertices;
    bool ShowEdges = false;
    BoundingSphere Bounds;
};

class Object3D
{
public:
    Object3D() = default;
    virtual ~Object3D();
    template<typename T>
    bool Load(const std::string& filepath);
    template<typename T>
    bool LoadFromPath(const std::filesystem::path& filepath);
    virtual void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false);
    virtual void DrawPickup(const glm::mat4& view, const glm::mat4& proj,
        const Ref<Shader>& shader, int objectID, bool xzInput = false, float xzInputY = 0.0f);
    virtual void DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
        const Ref<Shader>& shader, bool xzInput = false, float xzInputY = 0.0f);
    virtual void UpdateBoundingSphere();
    BoundingSphere GetWorldBoundingSphere() const;

    virtual size_t GetSubElementCount() const;
    virtual std::string GetSubElementName(size_t index) const;
    virtual bool IsSubElementVisible(size_t index) const;
    virtual void SetSubElementVisible(size_t index, bool visible);
    virtual size_t GetSubElementLineCount(size_t index) const;
    virtual int GetSelectedSubElementIndex() const { return m_SelectedSubElementIndex; }
    virtual bool IsSubElementSelected(int index) const { return index == m_SelectedSubElementIndex; }
    virtual void SetSelectedSubElementIndex(int index);
    virtual void ClearSelectedSubElement() { SetSelectedSubElementIndex(-1); }

    Transform Transfm;
	std::vector<Ref<Mesh>> Meshes;
    BoundingSphere Bounds;
    float Opacity = 1.0f;

protected:
    void ReleaseObjectElements();
    std::vector<Object3DElement> m_ObjectElements;
    int m_SelectedSubElementIndex = -1;
};

extern template bool Object3D::Load<VertexColor>(const std::string& filepath);
extern template bool Object3D::Load<VertexNormal>(const std::string& filepath);
