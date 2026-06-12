#include "stdsfx.h"
#include "Renderer/TerrainCDLOD.h"

#include <Image.h>
#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>
#include <Renderer/Shader.h>
#include <Renderer/VertexArray.h>

#include <algorithm>
#include <fstream>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#ifdef G_OPENGL
#include <glad/glad.h>
#endif

namespace
{
uint16_t ReadU16LE(const std::vector<uint8_t>& data, size_t offset)
{
    return (uint16_t)(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadU32LE(const std::vector<uint8_t>& data, size_t offset)
{
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
        ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
}

float ClampDistance(float value)
{
    return std::max(value, 0.0f);
}

const char* TerrainVertexShader()
{
    return R"(
#version 330 core
layout(location = 0) in vec2 a_Position;

uniform mat4 u_View;
uniform mat4 u_Projection;
uniform vec4 u_Node;
uniform vec4 u_MapMinSizeX;
uniform vec4 u_MapMinSizeY;
uniform vec4 u_HeightmapInfo;
uniform vec4 u_MorphConsts;
uniform vec3 u_CameraPos;
uniform sampler2D u_Heightmap;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TerrainUV;
out float v_LODLevel;
out float v_Morph;

float sampleHeight(vec2 uv)
{
    return textureLod(u_Heightmap, uv, 0.0).r;
}

vec2 rasterToWorld2D(vec2 raster)
{
    return vec2(
        u_MapMinSizeX.x + u_MapMinSizeX.y * raster.x / max(u_HeightmapInfo.x - 1.0, 1.0),
        u_MapMinSizeY.x + u_MapMinSizeY.y * raster.y / max(u_HeightmapInfo.y - 1.0, 1.0));
}

vec3 terrainPosition(vec2 raster)
{
    vec2 clampedRaster = clamp(raster, vec2(0.0), u_HeightmapInfo.xy - vec2(1.0));
    vec2 uv = (clampedRaster + vec2(0.5)) * u_HeightmapInfo.zw;
    float height = u_MapMinSizeX.z + sampleHeight(uv) * u_MapMinSizeX.w;
    vec2 worldXZ = rasterToWorld2D(clampedRaster);
    return vec3(worldXZ.x, height, worldXZ.y);
}

void main()
{
    float nodeSize = u_Node.z;
    float lodLevel = u_Node.w;
    vec2 unmorphedRaster = u_Node.xy + a_Position * nodeSize;
    vec3 unmorphedWorld = terrainPosition(unmorphedRaster);

    float eyeDist = distance(unmorphedWorld, u_CameraPos);
    float morphLerp = 1.0 - clamp(u_MorphConsts.z - eyeDist * u_MorphConsts.w, 0.0, 1.0);

    vec2 fineCell = a_Position * 32.0;
    vec2 snapBack = fract(fineCell * 0.5) * (2.0 / 32.0) * nodeSize;
    vec2 morphedRaster = unmorphedRaster - snapBack * morphLerp;
    vec3 worldPos = terrainPosition(morphedRaster);

    vec2 texel = vec2(1.0, 1.0);
    vec3 px0 = terrainPosition(morphedRaster - vec2(texel.x, 0.0));
    vec3 px1 = terrainPosition(morphedRaster + vec2(texel.x, 0.0));
    vec3 py0 = terrainPosition(morphedRaster - vec2(0.0, texel.y));
    vec3 py1 = terrainPosition(morphedRaster + vec2(0.0, texel.y));
    v_Normal = normalize(cross(py1 - py0, px1 - px0));
    if (v_Normal.y < 0.0)
        v_Normal = -v_Normal;

    v_WorldPos = worldPos;
    v_LODLevel = lodLevel;
    v_Morph = morphLerp;
    v_TerrainUV = (clamp(morphedRaster, vec2(0.0), u_HeightmapInfo.xy - vec2(1.0)) + vec2(0.5)) * u_HeightmapInfo.zw;
    gl_Position = u_Projection * u_View * vec4(worldPos, 1.0);
}
)";
}

const char* TerrainFragmentShader()
{
    return R"(
#version 330 core
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 geometryColor;
layout(location = 2) out vec4 normalColor;

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TerrainUV;
in float v_LODLevel;
in float v_Morph;

uniform vec3 u_CameraPos;
uniform vec4 u_MapMinSizeX;
uniform sampler2D u_OverlayMap;
uniform int u_TransparentPass;

void main()
{
    if (u_TransparentPass != 0)
        discard;

    vec3 lightDir = normalize(vec3(-0.35, 0.8, -0.45));
    float diffuse = max(dot(normalize(v_Normal), lightDir), 0.0);
    vec3 overlayColor = texture(u_OverlayMap, v_TerrainUV).rgb;
    vec3 lodTint = 0.08 * vec3(fract(v_LODLevel * 0.37), fract(v_LODLevel * 0.61), fract(v_LODLevel * 0.19));
    vec3 baseColor = overlayColor + lodTint;
    vec3 shaded = baseColor * (0.25 + diffuse * 0.75);

    color = vec4(shaded, 1.0);
    geometryColor = vec4(v_WorldPos, 1.0);
    normalColor = vec4(normalize(v_Normal), 1.0);
}
)";
}
}

TerrainCDLOD::TerrainCDLOD() = default;

TerrainCDLOD::TerrainCDLOD(const std::filesystem::path& heightmapPath)
{
    Load(heightmapPath, DefaultOverlayPath());
}

TerrainCDLOD::TerrainCDLOD(const std::filesystem::path& heightmapPath, const std::filesystem::path& overlayPath)
{
    Load(heightmapPath, overlayPath);
}

TerrainCDLOD::~TerrainCDLOD()
{
    DestroyGLResources();
    GeometryLibrary::Release(m_GridGeometry);
}

std::filesystem::path TerrainCDLOD::DefaultHeightmapPath()
{
    return std::filesystem::u8path("D:/gitclones/CDLOD/binaries_tools_testdata/hetch/heightmap.tif");
}

std::filesystem::path TerrainCDLOD::DefaultOverlayPath()
{
    return std::filesystem::u8path("D:/gitclones/CDLOD/binaries_tools_testdata/hetch/overlaymap.jpg");
}

bool TerrainCDLOD::Load(const std::filesystem::path& heightmapPath)
{
    return Load(heightmapPath, DefaultOverlayPath());
}

bool TerrainCDLOD::Load(const std::filesystem::path& heightmapPath, const std::filesystem::path& overlayPath)
{
    m_Loaded = false;
    m_SelectedNodes.clear();
    m_MinMaxLevels.clear();
    m_LODRanges.clear();
    m_MorphStart.clear();
    m_MorphEnd.clear();
    DestroyGLResources();
    GeometryLibrary::Release(m_GridGeometry);
    m_GridGeometry = {};

    if (!LoadTiffHeightmap(heightmapPath))
        return false;

    BuildMinMaxLevels();
    CreateHeightTexture();
    if (!CreateOverlayTexture(overlayPath))
        return false;
    CreateGridGeometry();
    CreateShader();
    UpdateBoundingSphere();

    m_Loaded = m_HeightTexture != 0 && m_OverlayTexture != 0 && m_GridGeometry.IsValid() && m_Shader != nullptr;
    return m_Loaded;
}

bool TerrainCDLOD::LoadTiffHeightmap(const std::filesystem::path& heightmapPath)
{
    std::ifstream file(heightmapPath, std::ios::binary);
    if (!file)
    {
        ERROR("Failed to open CDLOD heightmap: {}", heightmapPath.u8string());
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 16 || bytes[0] != 'I' || bytes[1] != 'I' || ReadU16LE(bytes, 2) != 42)
    {
        ERROR("Unsupported TIFF heightmap header: {}", heightmapPath.u8string());
        return false;
    }

    const uint32_t ifdOffset = ReadU32LE(bytes, 4);
    if (ifdOffset + 2 > bytes.size())
        return false;

    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bitsPerSample = 0;
    uint16_t compression = 1;
    uint16_t samplesPerPixel = 1;
    uint32_t stripOffset = 0;
    uint32_t stripByteCount = 0;
    const uint16_t entryCount = ReadU16LE(bytes, ifdOffset);
    for (uint16_t i = 0; i < entryCount; ++i)
    {
        const size_t entry = ifdOffset + 2 + (size_t)i * 12;
        if (entry + 12 > bytes.size())
            return false;
        const uint16_t tag = ReadU16LE(bytes, entry);
        const uint16_t type = ReadU16LE(bytes, entry + 2);
        const uint32_t count = ReadU32LE(bytes, entry + 4);
        const uint32_t value = ReadU32LE(bytes, entry + 8);
        const uint32_t scalar = (type == 3 && count == 1) ? (value & 0xffffu) : value;

        switch (tag)
        {
        case 256: width = scalar; break;
        case 257: height = scalar; break;
        case 258: bitsPerSample = (uint16_t)scalar; break;
        case 259: compression = (uint16_t)scalar; break;
        case 273: stripOffset = scalar; break;
        case 277: samplesPerPixel = (uint16_t)scalar; break;
        case 279: stripByteCount = scalar; break;
        default: break;
        }
    }

    const uint64_t expectedBytes = (uint64_t)width * (uint64_t)height * sizeof(uint16_t);
    if (width == 0 || height == 0 || bitsPerSample != 16 || compression != 1 || samplesPerPixel != 1 ||
        stripOffset == 0 || stripByteCount < expectedBytes || (uint64_t)stripOffset + expectedBytes > bytes.size())
    {
        ERROR("Unsupported TIFF layout for CDLOD heightmap: {}", heightmapPath.u8string());
        return false;
    }

    m_Heightmap.Width = (int)width;
    m_Heightmap.Height = (int)height;
    m_Heightmap.Samples.resize((size_t)width * (size_t)height);
    for (size_t i = 0; i < m_Heightmap.Samples.size(); ++i)
        m_Heightmap.Samples[i] = ReadU16LE(bytes, stripOffset + i * sizeof(uint16_t));
    return true;
}

void TerrainCDLOD::BuildMinMaxLevels()
{
    m_MinMaxLevels.clear();
    m_MinMaxLevels.resize((size_t)m_Config.LODLevelCount);

    const int leaf = m_Config.LeafQuadTreeNodeSize;
    MinMaxLevel& base = m_MinMaxLevels[0];
    base.Width = (m_Heightmap.Width - 1 + leaf - 1) / leaf;
    base.Height = (m_Heightmap.Height - 1 + leaf - 1) / leaf;
    base.Values.resize((size_t)base.Width * (size_t)base.Height);

    for (int by = 0; by < base.Height; ++by)
    {
        for (int bx = 0; bx < base.Width; ++bx)
        {
            uint16_t minH = std::numeric_limits<uint16_t>::max();
            uint16_t maxH = 0;
            const int fromX = bx * leaf;
            const int fromY = by * leaf;
            const int toX = std::min(fromX + leaf, m_Heightmap.Width - 1);
            const int toY = std::min(fromY + leaf, m_Heightmap.Height - 1);
            for (int y = fromY; y <= toY; ++y)
            {
                for (int x = fromX; x <= toX; ++x)
                {
                    const uint16_t h = m_Heightmap.Samples[(size_t)x + (size_t)y * m_Heightmap.Width];
                    minH = std::min(minH, h);
                    maxH = std::max(maxH, h);
                }
            }
            base.Values[(size_t)bx + (size_t)by * base.Width] = { minH, maxH };
        }
    }

    for (int level = 1; level < m_Config.LODLevelCount; ++level)
    {
        const MinMaxLevel& previous = m_MinMaxLevels[(size_t)level - 1];
        MinMaxLevel& current = m_MinMaxLevels[(size_t)level];
        current.Width = (previous.Width + 1) / 2;
        current.Height = (previous.Height + 1) / 2;
        current.Values.resize((size_t)current.Width * (size_t)current.Height);
        for (int y = 0; y < current.Height; ++y)
        {
            for (int x = 0; x < current.Width; ++x)
            {
                uint16_t minH = std::numeric_limits<uint16_t>::max();
                uint16_t maxH = 0;
                for (int oy = 0; oy < 2; ++oy)
                {
                    for (int ox = 0; ox < 2; ++ox)
                    {
                        const int sx = x * 2 + ox;
                        const int sy = y * 2 + oy;
                        if (sx >= previous.Width || sy >= previous.Height)
                            continue;
                        const MinMax& mm = previous.Values[(size_t)sx + (size_t)sy * previous.Width];
                        minH = std::min(minH, mm.Min);
                        maxH = std::max(maxH, mm.Max);
                    }
                }
                current.Values[(size_t)x + (size_t)y * current.Width] = { minH, maxH };
            }
        }
    }

    m_TopNodeSize = m_Config.LeafQuadTreeNodeSize << (m_Config.LODLevelCount - 1);

    m_LODRanges.resize((size_t)m_Config.LODLevelCount);
    m_MorphStart.resize((size_t)m_Config.LODLevelCount);
    m_MorphEnd.resize((size_t)m_Config.LODLevelCount);

    float total = 0.0f;
    float current = 1.0f;
    for (int i = 0; i < m_Config.LODLevelCount; ++i)
    {
        total += current;
        current *= m_Config.LODLevelDistanceRatio;
    }
    const float section = m_Config.MaxViewRange / std::max(total, 0.0001f);
    float prev = 0.0f;
    current = 1.0f;
    for (int i = 0; i < m_Config.LODLevelCount; ++i)
    {
        m_MorphStart[(size_t)i] = prev + section * current * 0.66f;
        prev += section * current;
        m_MorphEnd[(size_t)i] = prev;
        m_LODRanges[(size_t)i] = prev;
        current *= m_Config.LODLevelDistanceRatio;
    }
}

void TerrainCDLOD::CreateHeightTexture()
{
#ifdef G_OPENGL
    if (m_HeightTexture)
        glDeleteTextures(1, &m_HeightTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_HeightTexture);
    glTextureStorage2D(m_HeightTexture, 1, GL_R16, m_Heightmap.Width, m_Heightmap.Height);
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_HeightTexture, 0, 0, 0, m_Heightmap.Width, m_Heightmap.Height,
        GL_RED, GL_UNSIGNED_SHORT, m_Heightmap.Samples.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glTextureParameteri(m_HeightTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_HeightTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_HeightTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_HeightTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#endif
}

bool TerrainCDLOD::CreateOverlayTexture(const std::filesystem::path& overlayPath)
{
#ifdef G_OPENGL
    if (m_OverlayTexture)
    {
        glDeleteTextures(1, &m_OverlayTexture);
        m_OverlayTexture = 0;
    }

    const std::string overlayPathString = overlayPath.u8string();
    Ref<Image> overlay = Image::Load(overlayPathString.c_str(), PixelType::BYTE);
    if (!overlay || !overlay->Data || overlay->Width <= 0 || overlay->Height <= 0 || overlay->Channel != 4)
    {
        ERROR("Failed to load CDLOD overlay texture: {}", overlayPathString);
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_OverlayTexture);
    glTextureStorage2D(m_OverlayTexture, 1, GL_RGBA8, overlay->Width, overlay->Height);
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_OverlayTexture, 0, 0, 0, overlay->Width, overlay->Height,
        GL_RGBA, GL_UNSIGNED_BYTE, overlay->Data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glTextureParameteri(m_OverlayTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_OverlayTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_OverlayTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_OverlayTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return m_OverlayTexture != 0;
#else
    (void)overlayPath;
    return false;
#endif
}

void TerrainCDLOD::CreateGridGeometry()
{
    const uint32_t gridDim = (uint32_t)(m_Config.LeafQuadTreeNodeSize * m_Config.RenderGridResolutionMult);
    struct GridVertex { glm::vec2 Position; };
    std::vector<GridVertex> vertices;
    vertices.reserve((size_t)(gridDim + 1) * (gridDim + 1));
    for (uint32_t y = 0; y <= gridDim; ++y)
        for (uint32_t x = 0; x <= gridDim; ++x)
            vertices.push_back({ glm::vec2((float)x / (float)gridDim, (float)y / (float)gridDim) });

    std::vector<uint32_t> indices;
    indices.reserve((size_t)gridDim * gridDim * 6);
    for (uint32_t y = 0; y < gridDim; ++y)
    {
        for (uint32_t x = 0; x < gridDim; ++x)
        {
            const uint32_t i0 = x + y * (gridDim + 1);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + (gridDim + 1);
            const uint32_t i3 = i2 + 1;
            indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }
    m_GridIndexCount = (uint32_t)indices.size();

    Ref<VertexArray> vertexArray = VertexArray::Create();
    Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()),
        (uint32_t)(vertices.size() * sizeof(GridVertex)));
    vertexBuffer->SetLayout({ { ShaderDataType::Float2, "a_Position", false } });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(), (uint32_t)indices.size()));
    vertexArray->Unbind();
    m_GridGeometry = GeometryLibrary::Register(vertexArray, "TerrainCDLODGrid");
}

void TerrainCDLOD::CreateShader()
{
    m_Shader = Shader::Create("TerrainCDLOD", TerrainVertexShader(), TerrainFragmentShader());
}

void TerrainCDLOD::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (!m_Loaded || !m_Shader || transparentPass)
        return;

    Ref<VertexArray> grid = GeometryLibrary::Resolve(m_GridGeometry);
    if (!grid)
        return;

    const glm::vec3 cameraPosition = glm::vec3(glm::inverse(view)[3]);
    SelectLOD(cameraPosition);

    m_Shader->Bind();
    m_Shader->SetMat4("u_View", view);
    m_Shader->SetMat4("u_Projection", proj);
    m_Shader->SetFloat3("u_CameraPos", cameraPosition);
    m_Shader->SetFloat4("u_MapMinSizeX", glm::vec4(m_Config.MapDims.MinX, m_Config.MapDims.SizeX,
        m_Config.MapDims.MinZ, m_Config.MapDims.SizeZ));
    m_Shader->SetFloat4("u_MapMinSizeY", glm::vec4(m_Config.MapDims.MinY, m_Config.MapDims.SizeY, 0.0f, 0.0f));
    m_Shader->SetFloat4("u_HeightmapInfo", glm::vec4((float)m_Heightmap.Width, (float)m_Heightmap.Height,
        1.0f / (float)m_Heightmap.Width, 1.0f / (float)m_Heightmap.Height));
    m_Shader->SetInt("u_Heightmap", 0);
    m_Shader->SetInt("u_OverlayMap", 1);
    m_Shader->SetInt("u_TransparentPass", transparentPass ? 1 : 0);

#ifdef G_OPENGL
    glBindTextureUnit(0, m_HeightTexture);
    glBindTextureUnit(1, m_OverlayTexture);
#endif

    for (const SelectedNode& node : m_SelectedNodes)
    {
        const auto morph = GetMorphConsts(node.LODLevel);
        m_Shader->SetFloat4("u_Node", glm::vec4((float)node.X, (float)node.Y, (float)node.Size, (float)node.LODLevel));
        m_Shader->SetFloat4("u_MorphConsts", glm::vec4(morph[0], morph[1], morph[2], morph[3]));
        RenderCommand::DrawIndexed(grid, m_GridIndexCount);
    }
}

void TerrainCDLOD::UpdateBoundingSphere()
{
    const glm::vec3 minPoint(m_Config.MapDims.MinX, m_Config.MapDims.MinZ, m_Config.MapDims.MinY);
    const glm::vec3 maxPoint(m_Config.MapDims.MinX + m_Config.MapDims.SizeX,
        m_Config.MapDims.MinZ + m_Config.MapDims.SizeZ,
        m_Config.MapDims.MinY + m_Config.MapDims.SizeY);
    Bounds.Center = (minPoint + maxPoint) * 0.5f;
    Bounds.Radius = glm::length(maxPoint - Bounds.Center);
    Bounds.Valid = true;
}

void TerrainCDLOD::SelectLOD(const glm::vec3& cameraPosition)
{
    m_SelectedNodes.clear();
    const int rasterMaxX = m_Heightmap.Width - 1;
    const int rasterMaxY = m_Heightmap.Height - 1;
    const int topCountX = (rasterMaxX + m_TopNodeSize - 1) / m_TopNodeSize;
    const int topCountY = (rasterMaxY + m_TopNodeSize - 1) / m_TopNodeSize;
    for (int y = 0; y < topCountY; ++y)
        for (int x = 0; x < topCountX; ++x)
            SelectNode(x * m_TopNodeSize, y * m_TopNodeSize, m_TopNodeSize, m_Config.LODLevelCount - 1, cameraPosition);
}

bool TerrainCDLOD::SelectNode(int x, int y, int size, int lodLevel, const glm::vec3& cameraPosition)
{
    if (x >= m_Heightmap.Width - 1 || y >= m_Heightmap.Height - 1)
        return true;

    const MinMax minMax = GetNodeMinMax(x, y, size, lodLevel);
    const float distance = DistanceToNodeAABB(x, y, size, minMax, cameraPosition);
    if (distance > m_LODRanges[(size_t)lodLevel])
        return false;

    bool childrenFullyHandled = false;
    if (lodLevel > 0 && distance < m_LODRanges[(size_t)lodLevel - 1])
    {
        childrenFullyHandled = true;
        const int half = size / 2;
        childrenFullyHandled &= SelectNode(x, y, half, lodLevel - 1, cameraPosition);
        childrenFullyHandled &= SelectNode(x + half, y, half, lodLevel - 1, cameraPosition);
        childrenFullyHandled &= SelectNode(x, y + half, half, lodLevel - 1, cameraPosition);
        childrenFullyHandled &= SelectNode(x + half, y + half, half, lodLevel - 1, cameraPosition);
    }

    if (!childrenFullyHandled)
        m_SelectedNodes.push_back({ x, y, size, lodLevel, minMax.Min, minMax.Max });

    return true;
}

TerrainCDLOD::MinMax TerrainCDLOD::GetNodeMinMax(int x, int y, int size, int lodLevel) const
{
    const MinMaxLevel& level = m_MinMaxLevels[(size_t)lodLevel];
    const int cellSize = m_Config.LeafQuadTreeNodeSize << lodLevel;
    const int mx = std::clamp(x / cellSize, 0, std::max(level.Width - 1, 0));
    const int my = std::clamp(y / cellSize, 0, std::max(level.Height - 1, 0));
    if (!level.Values.empty())
        return level.Values[(size_t)mx + (size_t)my * level.Width];
    return {};
}

float TerrainCDLOD::DistanceToNodeAABB(int x, int y, int size, const MinMax& minMax, const glm::vec3& cameraPosition) const
{
    const glm::vec3 cdCamera(cameraPosition.x, cameraPosition.z, cameraPosition.y);
    float minWorldX, minWorldY, maxWorldX, maxWorldY;
    const int x1 = std::min(x + size, m_Heightmap.Width - 1);
    const int y1 = std::min(y + size, m_Heightmap.Height - 1);
    minWorldX = m_Config.MapDims.MinX + m_Config.MapDims.SizeX * x / (float)(m_Heightmap.Width - 1);
    maxWorldX = m_Config.MapDims.MinX + m_Config.MapDims.SizeX * x1 / (float)(m_Heightmap.Width - 1);
    minWorldY = m_Config.MapDims.MinY + m_Config.MapDims.SizeY * y / (float)(m_Heightmap.Height - 1);
    maxWorldY = m_Config.MapDims.MinY + m_Config.MapDims.SizeY * y1 / (float)(m_Heightmap.Height - 1);
    const float minWorldZ = m_Config.MapDims.MinZ + m_Config.MapDims.SizeZ * minMax.Min / 65535.0f;
    const float maxWorldZ = m_Config.MapDims.MinZ + m_Config.MapDims.SizeZ * minMax.Max / 65535.0f;

    const float dx = ClampDistance(std::max(minWorldX - cdCamera.x, cdCamera.x - maxWorldX));
    const float dy = ClampDistance(std::max(minWorldY - cdCamera.y, cdCamera.y - maxWorldY));
    const float dz = ClampDistance(std::max(minWorldZ - cdCamera.z, cdCamera.z - maxWorldZ));
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

glm::vec3 TerrainCDLOD::RasterToWorld(int x, int y, uint16_t height) const
{
    return glm::vec3(
        m_Config.MapDims.MinX + m_Config.MapDims.SizeX * x / (float)(m_Heightmap.Width - 1),
        m_Config.MapDims.MinZ + m_Config.MapDims.SizeZ * height / 65535.0f,
        m_Config.MapDims.MinY + m_Config.MapDims.SizeY * y / (float)(m_Heightmap.Height - 1));
}

std::array<float, 4> TerrainCDLOD::GetMorphConsts(int lodLevel) const
{
    const float start = m_MorphStart[(size_t)lodLevel];
    const float end = m_MorphEnd[(size_t)lodLevel];
    const float denom = std::max(end - start, 0.0001f);
    return { start, end, end / denom, 1.0f / denom };
}

void TerrainCDLOD::DestroyGLResources()
{
#ifdef G_OPENGL
    if (m_HeightTexture)
    {
        glDeleteTextures(1, &m_HeightTexture);
        m_HeightTexture = 0;
    }
    if (m_OverlayTexture)
    {
        glDeleteTextures(1, &m_OverlayTexture);
        m_OverlayTexture = 0;
    }
#endif
}
