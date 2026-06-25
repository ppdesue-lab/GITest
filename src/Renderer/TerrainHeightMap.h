#pragma once

#include <Object3D.h>

#include <vector>

class Camera;
class Scene;

class TerrainHeightMap : public Object3D
{
public:
    struct Sample
    {
        glm::vec3 Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        float Height = 0.0f;
        glm::vec4 LayerWeights = glm::vec4(0.0f);
    };

    TerrainHeightMap(int gridSize = 512, float terrainSize = 512.0f);

    bool IsLoaded() const { return m_Loaded; }
    int GetGridSize() const { return m_GridSize; }
    float GetTerrainSize() const { return m_TerrainSize; }
    float GetWaterLevel() const { return m_WaterLevel; }
    float SampleHeight(float worldX, float worldZ) const;
    glm::vec3 SampleNormal(float worldX, float worldZ) const;
    uint32_t GetNormalHeightTexture() const { return m_NormalHeightTexture; }
    uint32_t GetLayerDefTexture() const { return m_LayerDefTexture; }

    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;
    void DrawWithClip(const glm::mat4& view, const glm::mat4 proj, const glm::vec4& clipPlane);
    void UpdateBoundingSphere() override;

private:
    void GenerateTerrain();
    void CreateGeometry();
    void CreateShader();
    void LoadTextures();
    void CreateDataTextures();
    const Sample& SampleAt(int x, int z) const;

    int m_GridSize = 256;
    int m_LayerDefSize = 1024;
    float m_TerrainSize = 512.0f;
    float m_WaterLevel = 0.0f;
    std::vector<Sample> m_Samples;
    std::vector<glm::vec4> m_LayerDefData;
    GeometryHandle m_Geometry;
    Ref<Shader> m_Shader;
    TextureHandle m_RockBumpTexture;
    TextureHandle m_SandBumpTexture;
    TextureHandle m_RockMicroBumpTexture;
    TextureHandle m_SandMicroBumpTexture;
    TextureHandle m_RockDiffuseTexture;
    TextureHandle m_SandDiffuseTexture;
    TextureHandle m_GrassDiffuseTexture;
    TextureHandle m_SlopeDiffuseTexture;
    TextureHandle m_WaterBumpTexture;
    uint32_t m_NormalHeightTexture = 0;
    uint32_t m_LayerDefTexture = 0;
    uint32_t m_IndexCount = 0;
    bool m_Loaded = false;
};

class WaterNode : public Object3D
{
public:
    explicit WaterNode(const Ref<TerrainHeightMap>& terrain);
    ~WaterNode() override;

    bool IsLoaded() const { return m_Loaded; }
    void PrepareSceneTextures(const Scene& scene, const Camera& camera, const glm::vec2& viewportSize);
    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;
    void UpdateBoundingSphere() override;

private:
    void CreateGeometry();
    void CreateShader();
    void LoadTextures();
    void EnsureSceneFramebuffers(uint32_t width, uint32_t height);
    void DestroySceneFramebuffers();
    void RenderSceneTexture(const Scene& scene, const glm::mat4& view, const glm::mat4& projection,
        uint32_t framebuffer, uint32_t width, uint32_t height, const glm::vec4& clipPlane);

    Ref<TerrainHeightMap> m_Terrain;
    GeometryHandle m_Geometry;
    Ref<Shader> m_Shader;
    TextureHandle m_WaterBumpTexture;
    uint32_t m_ReflectionFBO = 0;
    uint32_t m_ReflectionColorTexture = 0;
    uint32_t m_ReflectionDepthTexture = 0;
    uint32_t m_RefractionFBO = 0;
    uint32_t m_RefractionColorTexture = 0;
    uint32_t m_RefractionDepthTexture = 0;
    uint32_t m_SceneTextureWidth = 0;
    uint32_t m_SceneTextureHeight = 0;
    uint32_t m_IndexCount = 0;
    float m_TerrainSize = 512.0f;
    float m_WaterLevel = 0.0f;
    float m_ZNear = 1.0f;
    float m_ZFar = 25000.0f;
    bool m_Loaded = false;
};
