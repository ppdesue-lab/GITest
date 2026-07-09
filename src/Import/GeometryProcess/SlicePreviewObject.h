#pragma once

#include <GeometryProcess/GeometryProcess.h>
#include <Object3D.h>

class SlicePreviewObject : public Object3D
{
public:
    ~SlicePreviewObject() override;

    bool LoadFromContours(const GeometryProcess::SliceContours& contours);

    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;
    void DrawPickup(const glm::mat4& view, const glm::mat4& proj,
        const Ref<Shader>& shader, int objectID, bool xzInput = false, float xzInputY = 0.0f) override;
    void DrawSelectedMask(const glm::mat4& view, const glm::mat4& proj,
        const Ref<Shader>& shader, bool xzInput = false, float xzInputY = 0.0f) override;
    void UpdateBoundingSphere() override;

private:
    GeometryHandle m_LineGeometry;
    uint32_t m_LineVertexCount = 0;
};
