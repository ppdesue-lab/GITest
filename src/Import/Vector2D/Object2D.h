#pragma once

#include "Vector2DDocument.h"

#include <Object3D.h>
#include <Renderer/Shader.h>
#include <Renderer/VertexArray.h>

class Object2D : public Object3D
{
public:
    struct Object2DElement
    {
        std::string Name;
        Transform Transfm;
        std::vector<uint32_t> LineIndices;
        GeometryHandle Geometry;
        GeometryHandle SelectionGeometry;
        uint32_t VertexCount = 0;
        uint32_t SelectionVertexCount = 0;
        glm::vec2 Center = glm::vec2(0.0f);
        bool Visible = true;
    };

    Object2D() = default;
    ~Object2D() override;

    bool LoadFromDocument(const Vector2DDocument& document);
    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;
    void DrawPickup(const glm::mat4& view, const glm::mat4& proj,
        const Ref<Shader>& shader, int objectID, bool xzInput = false, float xzInputY = 0.0f) override;
    void DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
        const Ref<Shader>& shader, bool xzInput = false, float xzInputY = 0.0f) override;
    void DrawSubElementPickup(const glm::mat4& view, const glm::mat4& proj, const Ref<Shader>& shader);

    const std::string& GetSourceName() const { return m_SourceName; }
    size_t GetLineCount() const { return m_Lines.size(); }
    size_t GetSubElementCount() const { return m_SubElements.size(); }
    Object2DElement* GetSubElement(size_t index);
    const Object2DElement* GetSubElement(size_t index) const;
    Transform* GetSubElementTransform(int index);
    int GetSelectedSubElementIndex() const { return m_SelectedSubElementIndex; }
    bool IsSubElementSelected(int index) const;
    const std::vector<int>& GetSelectedSubElementIndices() const { return m_SelectedSubElementIndices; }
    bool GetObjectBounds(glm::vec3& minimum, glm::vec3& maximum) const;
    bool GetSelectedSubElementBounds(glm::vec3& minimum, glm::vec3& maximum) const;
    void SetSelectedSubElementIndex(int index);
    void SetSelectedSubElementIndices(const std::vector<int>& indices);
    void ClearSelectedSubElement() { SetSelectedSubElementIndex(-1); }

private:
    struct PatternVertex
    {
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec4 Color = glm::vec4(1.0f);
        float Distance = 0.0f;
    };

    void UpdateBoundsFromVertices(const std::vector<VertexColor>& vertices);
    void EnsurePatternShader();
    void ReleaseElementGeometries();
    void BuildElementGeometry(Object2DElement& element);
    void RebuildSelectedSubElementGeometry();
    Ref<VertexArray> BuildVertexArray(const std::vector<VertexColor>& vertices) const;

    std::string m_SourceName;
    std::vector<Vector2DLine> m_Lines;
    std::vector<Object2DElement> m_SubElements;
    GeometryHandle m_SelectedSubElementGeometry;
    uint32_t m_SelectedSubElementVertexCount = 0;
    int m_SelectedSubElementIndex = -1;
    std::vector<int> m_SelectedSubElementIndices;
    Ref<Shader> m_LineShader;
    Ref<Shader> m_DashedLineShader;
};
