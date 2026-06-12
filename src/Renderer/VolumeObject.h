#pragma once

#include <Object3D.h>
#include <filesystem>

class VolumeObject : public Object3D
{
public:
    explicit VolumeObject(const std::filesystem::path& filepath);
    ~VolumeObject() override;

    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;
    void UpdateBoundingSphere() override;

    bool IsLoaded() const { return m_Loaded; }
    const glm::uvec3& GetDimensions() const { return m_Dimensions; }
    int& StepCount() { return m_StepCount; }
    float& Density() { return m_Density; }
    float& Exposure() { return m_Exposure; }

private:
    bool LoadManixDat(const std::filesystem::path& filepath);
    void CreateCubeGeometry();
    void CreateTransferTexture();
    void CreateShader();
    void EnsureAccumulationResources(uint32_t width, uint32_t height);
    void ResetAccumulation();
    void DestroyGLResources();

    GeometryHandle m_CubeGeometry;
    Ref<Shader> m_Shader;
    Ref<Shader> m_CompositeShader;
    glm::uvec3 m_Dimensions = glm::uvec3(0);
    uint32_t m_VolumeTexture = 0;
    uint32_t m_OpacityTransferTexture = 0;
    uint32_t m_DiffuseTransferTexture = 0;
    uint32_t m_SpecularTransferTexture = 0;
    uint32_t m_RoughnessTransferTexture = 0;
    uint32_t m_AccumulationFBO = 0;
    uint32_t m_AccumulationTextures[2] = { 0, 0 };
    uint32_t m_FullscreenVAO = 0;
    uint32_t m_AccumulationWidth = 0;
    uint32_t m_AccumulationHeight = 0;
    uint32_t m_CurrentAccumulation = 0;
    uint32_t m_FrameIndex = 0;
    glm::mat4 m_LastWorldViewProjection = glm::mat4(1.0f);
    bool m_HasLastWorldViewProjection = false;
    bool m_Loaded = false;
    int m_StepCount = 256;
    float m_Density = 24.0f;
    float m_Exposure = 12.0f;
};
