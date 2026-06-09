#pragma once

#include <Object3D.h>

#include <array>
#include <filesystem>

class TerrainCDLOD : public Object3D
{
public:
    struct MapDimensions
    {
        float MinX = -5690.0f;
        float MinY = -7090.0f;
        float MinZ = 1150.0f;
        float SizeX = 11380.0f;
        float SizeY = 14180.0f;
        float SizeZ = 1500.0f;
    };

    struct Config
    {
        MapDimensions MapDims;
        int LeafQuadTreeNodeSize = 8;
        int RenderGridResolutionMult = 4;
        int LODLevelCount = 7;
        float LODLevelDistanceRatio = 2.0f;
        float MinViewRange = 5000.0f;
        float MaxViewRange = 50000.0f;
    };

    TerrainCDLOD();
    explicit TerrainCDLOD(const std::filesystem::path& heightmapPath);
    TerrainCDLOD(const std::filesystem::path& heightmapPath, const std::filesystem::path& overlayPath);
    ~TerrainCDLOD() override;

    bool Load(const std::filesystem::path& heightmapPath);
    bool Load(const std::filesystem::path& heightmapPath, const std::filesystem::path& overlayPath);
    bool IsLoaded() const { return m_Loaded; }

    void Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass = false) override;
    void UpdateBoundingSphere() override;

    static std::filesystem::path DefaultHeightmapPath();
    static std::filesystem::path DefaultOverlayPath();

private:
    struct Heightmap
    {
        int Width = 0;
        int Height = 0;
        std::vector<uint16_t> Samples;
    };

    struct MinMax
    {
        uint16_t Min = 0;
        uint16_t Max = 0;
    };

    struct MinMaxLevel
    {
        int Width = 0;
        int Height = 0;
        std::vector<MinMax> Values;
    };

    struct SelectedNode
    {
        int X = 0;
        int Y = 0;
        int Size = 0;
        int LODLevel = 0;
        uint16_t MinHeight = 0;
        uint16_t MaxHeight = 0;
    };

    bool LoadTiffHeightmap(const std::filesystem::path& heightmapPath);
    void BuildMinMaxLevels();
    void CreateHeightTexture();
    bool CreateOverlayTexture(const std::filesystem::path& overlayPath);
    void CreateGridGeometry();
    void CreateShader();
    void SelectLOD(const glm::vec3& cameraPosition);
    bool SelectNode(int x, int y, int size, int lodLevel, const glm::vec3& cameraPosition);
    MinMax GetNodeMinMax(int x, int y, int size, int lodLevel) const;
    float DistanceToNodeAABB(int x, int y, int size, const MinMax& minMax, const glm::vec3& cameraPosition) const;
    glm::vec3 RasterToWorld(int x, int y, uint16_t height) const;
    std::array<float, 4> GetMorphConsts(int lodLevel) const;
    void DestroyGLResources();

    Config m_Config;
    Heightmap m_Heightmap;
    std::vector<MinMaxLevel> m_MinMaxLevels;
    std::vector<float> m_LODRanges;
    std::vector<float> m_MorphStart;
    std::vector<float> m_MorphEnd;
    std::vector<SelectedNode> m_SelectedNodes;

    GeometryHandle m_GridGeometry;
    Ref<Shader> m_Shader;
    uint32_t m_HeightTexture = 0;
    uint32_t m_OverlayTexture = 0;
    uint32_t m_GridIndexCount = 0;
    int m_TopNodeSize = 0;
    bool m_Loaded = false;
};
