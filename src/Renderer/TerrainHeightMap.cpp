#include "stdsfx.h"
#include "TerrainHeightMap.h"

#include <Application.h>
#include <Camera/Camera.h>
#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>
#include <Renderer/Shader.h>
#include <Renderer/Texture.h>
#include <Renderer/VertexArray.h>
#include <Renderer/VertexDesc.h>
#include <Scene.h>

#ifdef G_OPENGL
#include <glad/glad.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace
{
constexpr float kTerrainMaxHeight = 30.0f;
constexpr float kTerrainMinHeight = -30.0f;
constexpr float kTerrainFractalFactor = 0.68f;
constexpr float kTerrainFractalInitialValue = 100.0f;
constexpr int kTerrainSmoothSteps = 40;
constexpr int kTerrainLayerDefSize = 1024;
constexpr float kTerrainRockFactor = 0.95f;
constexpr float kTerrainSmoothFactor1 = 0.99f;
constexpr float kTerrainSmoothFactor2 = 0.10f;
constexpr float kTerrainHeightUnderwaterStart = -100.0f;
constexpr float kTerrainHeightUnderwaterEnd = -8.0f;
constexpr float kTerrainHeightSandStart = -30.0f;
constexpr float kTerrainHeightSandEnd = 1.7f;
constexpr float kTerrainHeightGrassStart = 1.7f;
constexpr float kTerrainHeightGrassEnd = 30.0f;
constexpr float kTerrainHeightRocksStart = -2.0f;
constexpr float kTerrainSlopeGrassStart = 0.96f;
constexpr float kTerrainSlopeRocksStart = 0.85f;
const glm::vec3 kIslandLightPosition(-10000.0f, 6500.0f, 10000.0f);

int WrapGrid(int value, int gridSize)
{
    if (value < 0)
        return value + gridSize;
    if (value >= gridSize)
        return value - gridSize;
    return value;
}

float Bilinear(float fx, float fz, float a, float b, float c, float d)
{
    const float x1 = a + (b - a) * fx;
    const float x2 = d + (c - d) * fx;
    return x1 + (x2 - x1) * fz;
}

float Frand()
{
    return static_cast<float>(std::rand() % 1000) / 1000.0f;
}

glm::vec4 LayerWeights(float height, float normalY)
{
    float underwater = 0.0f;
    float sand = 0.0f;
    float grass = 0.0f;
    float rock = 0.0f;

    if (height > kTerrainHeightUnderwaterStart && height <= kTerrainHeightUnderwaterEnd)
        underwater = 1.0f;
    if (height > kTerrainHeightSandStart && height <= kTerrainHeightSandEnd)
        sand = 1.0f;
    if (height > kTerrainHeightGrassStart && height <= kTerrainHeightGrassEnd)
        grass = 1.0f;
    if (normalY < kTerrainSlopeGrassStart && height > kTerrainHeightSandEnd)
        underwater = sand = grass = rock = 0.0f;
    if (normalY < kTerrainSlopeRocksStart && height > kTerrainHeightRocksStart)
    {
        underwater = sand = grass = 0.0f;
        rock = 1.0f;
    }

    const float total = std::max(underwater + sand + grass + rock, 1.0f);
    return glm::vec4(underwater, sand, grass, rock) / total;
}

float CurrentSeconds()
{
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<float>(clock::now() - start).count();
}

std::string IslandTexturePath(const char* filename)
{
    return GetFilePath(std::string("../data/textures/island11/") + filename);
}
}

TerrainHeightMap::TerrainHeightMap(int gridSize, float terrainSize)
    : m_GridSize(std::max(16, gridSize)), m_TerrainSize(std::max(16.0f, terrainSize))
{
    GenerateTerrain();
    CreateGeometry();
    CreateShader();
    LoadTextures();
    CreateDataTextures();
    UpdateBoundingSphere();
    m_Loaded = m_Geometry.IsValid() && m_Shader != nullptr;
}

const TerrainHeightMap::Sample& TerrainHeightMap::SampleAt(int x, int z) const
{
    const int n = m_GridSize + 1;
    x = std::clamp(x, 0, m_GridSize);
    z = std::clamp(z, 0, m_GridSize);
    return m_Samples[x + z * n];
}

float TerrainHeightMap::SampleHeight(float worldX, float worldZ) const
{
    if (m_Samples.empty())
        return 0.0f;

    const glm::mat4 model = Transfm.GetMatrix();
    const glm::vec3 localPos = glm::vec3(glm::inverse(model) * glm::vec4(worldX, 0.0f, worldZ, 1.0f));
    const float halfSize = m_TerrainSize * 0.5f;
    const float gx = glm::clamp((localPos.x + halfSize) / m_TerrainSize * (float)m_GridSize, 0.0f, (float)m_GridSize);
    const float gz = glm::clamp((localPos.z + halfSize) / m_TerrainSize * (float)m_GridSize, 0.0f, (float)m_GridSize);
    const int x = std::min((int)std::floor(gx), m_GridSize - 1);
    const int z = std::min((int)std::floor(gz), m_GridSize - 1);
    const float fx = gx - (float)x;
    const float fz = gz - (float)z;
    const float localHeight = Bilinear(fx, fz,
        SampleAt(x, z).Height,
        SampleAt(x + 1, z).Height,
        SampleAt(x + 1, z + 1).Height,
        SampleAt(x, z + 1).Height);
    return glm::vec3(model * glm::vec4(localPos.x, localHeight, localPos.z, 1.0f)).y;
}

glm::vec3 TerrainHeightMap::SampleNormal(float worldX, float worldZ) const
{
    const float cellSize = m_TerrainSize / (float)m_GridSize;
    const float hL = SampleHeight(worldX - cellSize, worldZ);
    const float hR = SampleHeight(worldX + cellSize, worldZ);
    const float hD = SampleHeight(worldX, worldZ - cellSize);
    const float hU = SampleHeight(worldX, worldZ + cellSize);
    return glm::normalize(glm::vec3(hL - hR, 2.0f * cellSize, hD - hU));
}

void TerrainHeightMap::GenerateTerrain()
{
    const int n = m_GridSize + 1;
    std::vector<float> back(n * n, 0.0f);
    std::vector<float> height(n * n, 0.0f);
    auto backAt = [&](std::vector<float>& data, int x, int z) -> float& {
        x = std::clamp(x, 0, m_GridSize);
        z = std::clamp(z, 0, m_GridSize);
        return data[x + z * m_GridSize];
    };
    auto backRead = [&](const std::vector<float>& data, int x, int z) -> float {
        x = std::clamp(x, 0, m_GridSize);
        z = std::clamp(z, 0, m_GridSize);
        return data[x + z * m_GridSize];
    };
    auto heightAt = [&](std::vector<float>& data, int x, int z) -> float& {
        x = std::clamp(x, 0, m_GridSize);
        z = std::clamp(z, 0, m_GridSize);
        return data[x + z * n];
    };
    auto heightRead = [&](const std::vector<float>& data, int x, int z) -> float {
        x = std::clamp(x, 0, m_GridSize);
        z = std::clamp(z, 0, m_GridSize);
        return data[x + z * n];
    };

    std::srand(12);
    int currentStep = m_GridSize;
    float randomMagnitude = kTerrainFractalInitialValue;
    while (currentStep > 1)
    {
        for (int x = 0; x < m_GridSize; x += currentStep)
        {
            for (int z = 0; z < m_GridSize; z += currentStep)
            {
                const float center = (backRead(back, x, z) + backRead(back, x + currentStep, z) +
                    backRead(back, x + currentStep, z + currentStep) + backRead(back, x, z + currentStep)) * 0.25f;
                backAt(back, x + currentStep / 2, z + currentStep / 2) = center + randomMagnitude * (Frand() - 0.5f);
            }
        }

        for (int x = 0; x < m_GridSize; x += currentStep)
        {
            for (int z = 0; z < m_GridSize; z += currentStep)
            {
                const int hs = currentStep / 2;
                backAt(back, x + hs, z) = (backRead(back, x, z) + backRead(back, x + currentStep, z) +
                    backRead(back, x + hs, z + hs) + backRead(back, x + hs, WrapGrid(z - hs, m_GridSize))) * 0.25f +
                    randomMagnitude * (Frand() - 0.5f);
                backAt(back, x, z + hs) = (backRead(back, x, z) + backRead(back, x, z + currentStep) +
                    backRead(back, x + hs, z + hs) + backRead(back, WrapGrid(x - hs, m_GridSize), z + hs)) * 0.25f +
                    randomMagnitude * (Frand() - 0.5f);
                backAt(back, x + currentStep, z + hs) = (backRead(back, x + currentStep, z) +
                    backRead(back, x + currentStep, z + currentStep) + backRead(back, x + hs, z + hs) +
                    backRead(back, WrapGrid(x + hs + currentStep, m_GridSize), z + hs)) * 0.25f +
                    randomMagnitude * (Frand() - 0.5f);
                backAt(back, x + hs, z + currentStep) = (backRead(back, x + currentStep, z + currentStep) +
                    backRead(back, x, z + currentStep) + backRead(back, x + hs, z + hs) +
                    backRead(back, x + hs, WrapGrid(z + hs + currentStep, m_GridSize))) * 0.25f +
                    randomMagnitude * (Frand() - 0.5f);
            }
        }

        currentStep /= 2;
        randomMagnitude *= kTerrainFractalFactor;
    }

    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
            heightAt(height, x, z) = backRead(back, x, z);
    }

    float minHeight = heightRead(height, 0, 0);
    float maxHeight = minHeight;
    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
        {
            const float h = heightRead(height, x, z);
            minHeight = std::min(minHeight, h);
            maxHeight = std::max(maxHeight, h);
        }
    }

    const float scale = (kTerrainMaxHeight - kTerrainMinHeight) / (maxHeight - minHeight);
    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
        {
            float h = (heightRead(height, x, z) - minHeight) * scale + kTerrainMinHeight;
            const float dx = x - m_GridSize * 0.5f;
            const float dz = z - m_GridSize * 0.5f;
            const float dist2 = dx * dx + dz * dz;
            const float coast = (m_GridSize * 0.8f) * (m_GridSize * 0.8f) * 0.25f;
            if (dist2 > coast)
                h -= (dist2 - coast) / 1000.0f;
            heightAt(height, x, z) = std::max(kTerrainMinHeight, h);
        }
    }

    for (int pass = 0; pass < 10; ++pass)
    {
        for (float& h : height)
        {
            if (h > 0.02f)
                h -= 0.02f;
            else if (h < -0.02f)
                h += 0.02f;
        }
    }

    for (int pass = 0; pass < kTerrainSmoothSteps; ++pass)
    {
        for (int z = 0; z < n; ++z)
        {
            for (int x = 0; x < n; ++x)
            {
                const glm::vec3 v1(2.0f, heightRead(height, WrapGrid(x + 1, m_GridSize), z) -
                    heightRead(height, WrapGrid(x - 1, m_GridSize), z), 0.0f);
                const glm::vec3 v2(0.0f, -(heightRead(height, x, WrapGrid(z + 1, m_GridSize)) -
                    heightRead(height, x, WrapGrid(z - 1, m_GridSize))), -2.0f);
                const glm::vec3 normal = glm::normalize(glm::cross(v1, v2));
                const float smooth = (normal.y > kTerrainRockFactor || heightRead(height, x, z) < 1.2f)
                    ? kTerrainSmoothFactor1 : kTerrainSmoothFactor2;
                backAt(back, x, z) = heightRead(height, x, z) * (1.0f - smooth) + smooth * 0.25f *
                    (heightRead(height, WrapGrid(x - 1, m_GridSize), z) + heightRead(height, x, WrapGrid(z - 1, m_GridSize)) +
                     heightRead(height, WrapGrid(x + 1, m_GridSize), z) + heightRead(height, x, WrapGrid(z + 1, m_GridSize)));
            }
        }
        for (int z = 0; z < n; ++z)
        {
            for (int x = 0; x < n; ++x)
                heightAt(height, x, z) = backRead(back, x, z);
        }
    }

    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
        {
            constexpr float smooth = 0.5f;
            backAt(back, x, z) = heightRead(height, x, z) * (1.0f - smooth) + smooth * 0.25f *
                (heightRead(height, WrapGrid(x - 1, m_GridSize), z) + heightRead(height, x, WrapGrid(z - 1, m_GridSize)) +
                 heightRead(height, WrapGrid(x + 1, m_GridSize), z) + heightRead(height, x, WrapGrid(z + 1, m_GridSize)));
        }
    }
    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
            heightAt(height, x, z) = backRead(back, x, z);
    }

    m_Samples.resize(n * n);
    std::vector<glm::vec3> normals(n * n);
    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
        {
            const glm::vec3 v1(2.0f, heightRead(height, WrapGrid(x + 1, m_GridSize), z) -
                heightRead(height, WrapGrid(x - 1, m_GridSize), z), 0.0f);
            const glm::vec3 v2(0.0f, -(heightRead(height, x, WrapGrid(z + 1, m_GridSize)) -
                heightRead(height, x, WrapGrid(z - 1, m_GridSize))), -2.0f);
            const glm::vec3 normal = glm::normalize(glm::cross(v1, v2));
            normals[x + z * n] = normal;
            Sample& sample = m_Samples[x + z * n];
            sample.Height = heightRead(height, x, z);
            sample.Normal = normal;
        }
    }

    m_LayerDefSize = kTerrainLayerDefSize;
    std::vector<glm::vec4> tempLayer((size_t)m_LayerDefSize * (size_t)m_LayerDefSize, glm::vec4(0.0f));
    auto normalY = [&](int x, int z) -> float {
        x = std::clamp(x, 0, m_GridSize);
        z = std::clamp(z, 0, m_GridSize);
        return normals[x + z * n].y;
    };

    for (int z = 0; z < m_LayerDefSize; ++z)
    {
        for (int x = 0; x < m_LayerDefSize; ++x)
        {
            const float tx = (float)m_GridSize * ((float)x / (float)m_LayerDefSize);
            const float tz = (float)m_GridSize * ((float)z / (float)m_LayerDefSize);
            const int ix = std::min(m_GridSize - 1, (int)std::floor(tx));
            const int iz = std::min(m_GridSize - 1, (int)std::floor(tz));
            const float fx = tx - (float)ix;
            const float fz = tz - (float)iz;
            const float h = Bilinear(fx, fz,
                heightRead(height, ix, iz),
                heightRead(height, ix + 1, iz),
                heightRead(height, ix + 1, iz + 1),
                heightRead(height, ix, iz + 1));
            const float ny = Bilinear(fx, fz,
                normalY(ix, iz),
                normalY(ix + 1, iz),
                normalY(ix + 1, iz + 1),
                normalY(ix, iz + 1));
            tempLayer[x + z * m_LayerDefSize] = LayerWeights(h, ny);
        }
    }

    m_LayerDefData = tempLayer;
    for (int z = 2; z < m_LayerDefSize - 2; ++z)
    {
        for (int x = 2; x < m_LayerDefSize - 2; ++x)
        {
            glm::vec4 sum(0.0f);
            for (int oz = -2; oz <= 2; ++oz)
                for (int ox = -2; ox <= 2; ++ox)
                    sum += tempLayer[(x + ox) + (z + oz) * m_LayerDefSize];
            m_LayerDefData[x + z * m_LayerDefSize] = sum / 25.0f;
        }
    }

    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
        {
            const int lx = std::clamp((int)((float)x / (float)m_GridSize * (float)m_LayerDefSize), 0, m_LayerDefSize - 1);
            const int lz = std::clamp((int)((float)z / (float)m_GridSize * (float)m_LayerDefSize), 0, m_LayerDefSize - 1);
            m_Samples[x + z * n].LayerWeights = m_LayerDefData[lx + lz * m_LayerDefSize];
        }
    }
}

void TerrainHeightMap::CreateGeometry()
{
    struct GridVertex
    {
        glm::vec2 XZ;
    };

    const int n = m_GridSize + 1;
    std::vector<GridVertex> vertices;
    vertices.reserve(n * n);
    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
            vertices.push_back({ glm::vec2((float)x, (float)z) });
    }

    std::vector<uint32_t> indices;
    indices.reserve((size_t)m_GridSize * (size_t)m_GridSize * 6u);
    for (int z = 0; z < m_GridSize; ++z)
    {
        for (int x = 0; x < m_GridSize; ++x)
        {
            const uint32_t i0 = (uint32_t)(x + z * n);
            const uint32_t i1 = (uint32_t)(x + 1 + z * n);
            const uint32_t i2 = (uint32_t)(x + 1 + (z + 1) * n);
            const uint32_t i3 = (uint32_t)(x + (z + 1) * n);
            indices.insert(indices.end(), { i0, i1, i2, i0, i2, i3 });
        }
    }

    Ref<VertexArray> vertexArray = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()),
        (uint32_t)(vertices.size() * sizeof(GridVertex)));
    vertexBuffer->SetLayout({
        { ShaderDataType::Float2, "aXZ", false }
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(), (uint32_t)indices.size()));
    vertexArray->Unbind();

    m_Geometry = GeometryLibrary::Register(vertexArray);
    m_IndexCount = (uint32_t)indices.size();

    auto mesh = CreateRef<Mesh>();
    mesh->VertexObject = m_Geometry;
    mesh->TraceIndices = indices;
    Meshes.clear();
    Meshes.push_back(mesh);
}

void TerrainHeightMap::CreateShader()
{
    auto library = Application::Get().GetShaderLibrary();
    if (library->Exists("TerrainHeightMap"))
    {
        m_Shader = library->Get("TerrainHeightMap");
        return;
    }

    const std::string vertexSource = R"(
        #version 400 core
        layout(location = 0) in vec2 aXZ;
        uniform mat4 uMvp;
        uniform mat4 uModel;
        uniform sampler2D uHeightMap;
        uniform sampler2D uLayerDef;
        uniform sampler2D uSandBump;
        uniform sampler2D uRockBump;
        uniform float uTerrainSize;
        uniform vec3 uCameraPos;
        out vec3 vWorldPos;
        out vec3 vNormal;
        out float vHeight;
        out vec2 vUv;
        out vec2 vDetailUv;
        out vec4 vLayerDef;
        out float vDetailHeight;

        void main()
        {
            vec2 xz = aXZ;
            vec2 uv = xz / uTerrainSize;
            vec2 sampleUv = vec2(uv.x, 1.0 - uv.y);
            vec4 normalHeight = texture(uHeightMap, sampleUv);
            vec3 baseNormal = normalize(vec3(normalHeight.x, normalHeight.y, -normalHeight.z));
            vec4 layerDef = texture(uLayerDef, sampleUv);

            vec4 sandDetail = textureLod(uSandBump, sampleUv * vec2(3.5), 0.0).rbga;
            vec4 rockDetail = textureLod(uRockBump, sampleUv * vec2(10.0), 0.0).rbga;
            vec3 sandNormal = normalize(2.0 * sandDetail.xyz - vec3(1.0, 0.0, 1.0));
            vec3 rockNormal = normalize(2.0 * rockDetail.xyz - vec3(1.0, 1.4, 1.0));
            vec3 detailNormal = normalize(mix(sandNormal, rockNormal, layerDef.w));
            float detailHeight = mix((sandDetail.w - 0.5) * 0.5, (rockDetail.w - 0.5) * 3.0, layerDef.w);

            vec3 tangentZ = normalize(cross(vec3(-1.0, 0.0, 0.0), baseNormal));
            vec3 tangentX = normalize(cross(tangentZ, baseNormal));
            vec3 rotatedDetailNormal = normalize(mat3(tangentX, baseNormal, tangentZ) * detailNormal);

            vec3 localPos = vec3(xz.x, normalHeight.w, xz.y) + baseNormal * detailHeight;
            vec3 worldPos = vec3(uModel * vec4(localPos, 1.0));
            vWorldPos = worldPos;
            vNormal = normalize(mat3(uModel) * rotatedDetailNormal);
            vHeight = worldPos.y;
            vUv = sampleUv;
            vDetailUv = sampleUv * vec2(130.0);
            vLayerDef = layerDef;
            vDetailHeight = detailHeight;
            gl_Position = uMvp * vec4(localPos, 1.0);
        }
    )";

    const std::string fragmentSource = R"(
        #version 400 core
        layout(location = 0) out vec4 color;
        layout(location = 1) out vec4 gPosition;
        layout(location = 2) out vec4 gNormal;
        in vec3 vWorldPos;
        in vec3 vNormal;
        in float vHeight;
        in vec2 vUv;
        in vec2 vDetailUv;
        in vec4 vLayerDef;
        in float vDetailHeight;
        uniform vec3 uCameraPos;
        uniform float uTime;
        uniform sampler2D uSandDiffuse;
        uniform sampler2D uRockDiffuse;
        uniform sampler2D uGrassDiffuse;
        uniform sampler2D uSlopeDiffuse;
        uniform sampler2D uSandMicroBump;
        uniform sampler2D uRockMicroBump;
        uniform sampler2D uWaterBump;
        uniform float uTerrainSize;
        uniform vec3 uLightPos;
        uniform float uHalfSpaceCullSign;
        uniform float uHalfSpaceCullPosition;

        vec2 waterShift()
        {
            return vec2(uTime * 1.5, uTime * 0.75);
        }

        vec3 combineSimplifiedWaterNormal(vec3 worldPos, float mipLevel)
        {
            vec3 waterNormal = vec3(0.0, 4.0, 0.0);
            vec2 tc = worldPos.xz * vec2(7.0) / uTerrainSize;
            vec2 variance = vec2(1.0, 1.0);
            float texcoordScale = 1.0;
            float normalScale = 1.0;
            for (int i = 0; i < 8; ++i)
            {
                vec4 t = textureLod(uWaterBump, tc * texcoordScale + waterShift() * 0.03 * variance, mipLevel).rbga;
                variance.x *= -1.0;
                waterNormal.xz += (2.0 * t.xz - vec2(1.0)) * normalScale;
                texcoordScale *= 1.4;
                normalScale *= 0.85;
            }
            return normalize(waterNormal);
        }

        float calculateWaterCausticIntensity(vec3 worldPos)
        {
            if (worldPos.y > -0.15)
                return 0.0;

            vec3 pixelToLight = normalize(uLightPos - worldPos);
            float waterDepth = 0.5 - worldPos.y;
            vec3 surfacePos = worldPos;
            surfacePos.xz -= worldPos.y * pixelToLight.xz;
            vec3 pixelToSurfaceLight = pixelToLight * waterDepth;

            float cc = 0.0;
            float m = 0.2;
            float k = 0.15;
            for (int ix = -3; ix <= 3; ++ix)
            {
                for (int iz = -3; iz <= 3; ++iz)
                {
                    vec2 offset = vec2(float(ix), float(iz)) * k * m * waterDepth;
                    vec3 n = combineSimplifiedWaterNormal(vec3(surfacePos.x + offset.x, 0.0, surfacePos.z + offset.y), 0.0);
                    vec3 refracted = m * (pixelToSurfaceLight + vec3(float(ix) * k, 0.0, float(iz) * k)) - 0.5 * vec3(n.x, 0.0, n.z);
                    cc += 0.05 * max(0.0, pow(max(0.0, dot(normalize(refracted), normalize(pixelToLight))), 500.0));
                }
            }

            cc *= 200.0 / (200.0 + distance(uCameraPos, worldPos));
            cc *= clamp(-1.0 - worldPos.y, 0.0, 1.0);
            return cc;
        }

        void main()
        {
            if ((vWorldPos.y - uHalfSpaceCullPosition) * uHalfSpaceCullSign < 0.0)
                discard;

            vec3 n = normalize(vNormal);
            vec3 micro = normalize(2.0 * texture(uSandMicroBump, vDetailUv).rbg - vec3(1.0));
            micro = normalize(mix(micro, 2.0 * texture(uRockMicroBump, vDetailUv).rbg - vec3(1.0), vLayerDef.w));
            vec3 tangentZ = normalize(cross(vec3(-1.0, 0.0, 0.0), n));
            vec3 tangentX = normalize(cross(tangentZ, n));
            micro = normalize(mat3(tangentX, n, tangentZ) * micro);

            vec4 albedo = texture(uSlopeDiffuse, vDetailUv);
            albedo = mix(albedo, texture(uSandDiffuse, vDetailUv), vLayerDef.g * vLayerDef.g);
            albedo = mix(albedo, texture(uRockDiffuse, vDetailUv), vLayerDef.w * vLayerDef.w);
            albedo = mix(albedo, texture(uGrassDiffuse, vDetailUv), vLayerDef.b);
            albedo.rgb *= 0.5 + 0.5 * clamp(vDetailHeight / 3.0 + 0.5, 0.0, 1.0);

            vec3 l = normalize(uLightPos - vWorldPos);
            float ndl = max(dot(micro, l), 0.0);
            vec3 lit = albedo.rgb * (0.20 + ndl);
            lit += 0.2 * max(0.0, dot(vec3(0.0, 1.0, 0.0), micro)) * vec3(0.2, 0.2, 0.3);
            lit *= 0.5 + 0.8 * clamp(vWorldPos.y * 0.5 + 0.5, 0.0, 1.0);
            float caustic = calculateWaterCausticIntensity(vWorldPos);
            lit *= 1.0 + caustic * max(0.0, 0.4 + 0.6 * ndl);
            float fog = clamp(exp(-distance(uCameraPos, vWorldPos) / 820.0), 0.0, 1.0);
            vec3 fogColor = mix(vec3(0.52, 0.62, 0.75), vec3(0.86, 0.92, 1.0), max(l.y, 0.0));
            vec3 outColor = mix(fogColor, lit, fog);

            color = vec4(outColor, 1.0);
            gPosition = vec4(vWorldPos, 1.0);
            gNormal = vec4(normalize(micro), 1.0);
        }
    )";

    m_Shader = library->Load("TerrainHeightMap", vertexSource, fragmentSource);
}

void TerrainHeightMap::LoadTextures()
{
    m_RockBumpTexture = TextureLibrary::LoadTexture(IslandTexturePath("rock_bump6.dds"));
    m_SandBumpTexture = TextureLibrary::LoadTexture(IslandTexturePath("rock_bump4.dds"));
    m_RockMicroBumpTexture = TextureLibrary::LoadTexture(IslandTexturePath("rock_bump4.dds"));
    m_SandMicroBumpTexture = TextureLibrary::LoadTexture(IslandTexturePath("lichen1_normal.dds"));
    m_RockDiffuseTexture = TextureLibrary::LoadTexture(IslandTexturePath("terrain_rock4.dds"));
    m_SandDiffuseTexture = TextureLibrary::LoadTexture(IslandTexturePath("sand_diffuse.dds"));
    m_GrassDiffuseTexture = TextureLibrary::LoadTexture(IslandTexturePath("terrain_grass.dds"));
    m_SlopeDiffuseTexture = TextureLibrary::LoadTexture(IslandTexturePath("terrain_slope.dds"));
    m_WaterBumpTexture = TextureLibrary::LoadTexture(IslandTexturePath("water_bump.dds"));
}

void TerrainHeightMap::CreateDataTextures()
{
#ifdef G_OPENGL
    if (m_Samples.empty())
        return;

    std::vector<float> normalHeight((size_t)m_GridSize * (size_t)m_GridSize * 4u);
    std::vector<float> layerDef((size_t)m_LayerDefSize * (size_t)m_LayerDefSize * 4u);
    for (int z = 0; z < m_GridSize; ++z)
    {
        for (int x = 0; x < m_GridSize; ++x)
        {
            const Sample& sample = SampleAt(x, z);
            const size_t index = ((size_t)x + (size_t)z * (size_t)m_GridSize) * 4u;
            normalHeight[index + 0] = sample.Normal.x;
            normalHeight[index + 1] = sample.Normal.y;
            normalHeight[index + 2] = sample.Normal.z;
            normalHeight[index + 3] = sample.Height;
        }
    }
    for (int z = 0; z < m_LayerDefSize; ++z)
    {
        for (int x = 0; x < m_LayerDefSize; ++x)
        {
            const glm::vec4 weights = m_LayerDefData.empty()
                ? glm::vec4(0.0f)
                : m_LayerDefData[(size_t)x + (size_t)z * (size_t)m_LayerDefSize];
            const size_t index = ((size_t)x + (size_t)z * (size_t)m_LayerDefSize) * 4u;
            layerDef[index + 0] = weights.r;
            layerDef[index + 1] = weights.g;
            layerDef[index + 2] = weights.b;
            layerDef[index + 3] = weights.a;
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_NormalHeightTexture);
    glTextureStorage2D(m_NormalHeightTexture, 1, GL_RGBA32F, m_GridSize, m_GridSize);
    glTextureSubImage2D(m_NormalHeightTexture, 0, 0, 0, m_GridSize, m_GridSize, GL_RGBA, GL_FLOAT, normalHeight.data());
    glTextureParameteri(m_NormalHeightTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_NormalHeightTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_NormalHeightTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_NormalHeightTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_LayerDefTexture);
    glTextureStorage2D(m_LayerDefTexture, 1, GL_RGBA32F, m_LayerDefSize, m_LayerDefSize);
    glTextureSubImage2D(m_LayerDefTexture, 0, 0, 0, m_LayerDefSize, m_LayerDefSize, GL_RGBA, GL_FLOAT, layerDef.data());
    glTextureParameteri(m_LayerDefTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_LayerDefTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_LayerDefTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_LayerDefTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
#endif
}

void TerrainHeightMap::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass)
        return;
    DrawWithClip(view, proj, glm::vec4(0.0f, 1.0f, 0.0f, -std::numeric_limits<float>::max()));
}

void TerrainHeightMap::DrawWithClip(const glm::mat4& view, const glm::mat4 proj, const glm::vec4& clipPlane)
{
    if (!m_Shader)
        return;

    Ref<VertexArray> vertexArray = GeometryLibrary::Resolve(m_Geometry);
    if (!vertexArray)
        return;

    const float time = CurrentSeconds();
    m_Shader->Bind();
    const glm::mat4 model = Transfm.GetMatrix();
    m_Shader->SetMat4("uMvp", proj * view * model);
    m_Shader->SetMat4("uModel", model);
    m_Shader->SetFloat3("uCameraPos", glm::vec3(glm::inverse(view)[3]));
    m_Shader->SetFloat3("uLightPos", kIslandLightPosition);
    m_Shader->SetFloat("uTerrainSize", (float)m_GridSize);
    m_Shader->SetFloat("uTime", time);
    const bool enableClipPlane = clipPlane.w > -1000000.0f;
    m_Shader->SetFloat("uHalfSpaceCullSign", enableClipPlane ? clipPlane.y : 1.0f);
    m_Shader->SetFloat("uHalfSpaceCullPosition", enableClipPlane ? -clipPlane.w : kTerrainMinHeight * 2.0f);
    m_Shader->SetInt("uHeightMap", 0);
    m_Shader->SetInt("uLayerDef", 1);
    m_Shader->SetInt("uSandBump", 2);
    m_Shader->SetInt("uRockBump", 3);
    m_Shader->SetInt("uSandDiffuse", 4);
    m_Shader->SetInt("uRockDiffuse", 5);
    m_Shader->SetInt("uGrassDiffuse", 6);
    m_Shader->SetInt("uSlopeDiffuse", 7);
    m_Shader->SetInt("uSandMicroBump", 8);
    m_Shader->SetInt("uRockMicroBump", 9);
    m_Shader->SetInt("uWaterBump", 10);
#ifdef G_OPENGL
    glBindTextureUnit(0, m_NormalHeightTexture);
    glBindTextureUnit(1, m_LayerDefTexture);
#endif
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_SandBumpTexture)) texture->Bind(2);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_RockBumpTexture)) texture->Bind(3);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_SandDiffuseTexture)) texture->Bind(4);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_RockDiffuseTexture)) texture->Bind(5);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_GrassDiffuseTexture)) texture->Bind(6);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_SlopeDiffuseTexture)) texture->Bind(7);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_SandMicroBumpTexture)) texture->Bind(8);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_RockMicroBumpTexture)) texture->Bind(9);
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_WaterBumpTexture)) texture->Bind(10);
    RenderCommand::DrawIndexed(vertexArray, m_IndexCount);
}

void TerrainHeightMap::UpdateBoundingSphere()
{
    Bounds.Center = glm::vec3(m_TerrainSize * 0.5f, 0.0f, m_TerrainSize * 0.5f);
    Bounds.Radius = std::sqrt(m_TerrainSize * m_TerrainSize * 0.5f + kTerrainMaxHeight * kTerrainMaxHeight);
    Bounds.Valid = true;
}

WaterNode::WaterNode(const Ref<TerrainHeightMap>& terrain)
    : m_Terrain(terrain)
{
    if (m_Terrain)
    {
        m_TerrainSize = m_Terrain->GetTerrainSize();
        m_WaterLevel = m_Terrain->GetWaterLevel();
    }
    Opacity = 1.0f;
    CreateGeometry();
    CreateShader();
    LoadTextures();
    UpdateBoundingSphere();
    m_Loaded = m_Terrain != nullptr && m_Geometry.IsValid() && m_Shader != nullptr;
}

WaterNode::~WaterNode()
{
    DestroySceneFramebuffers();
}

void WaterNode::CreateGeometry()
{
    struct GridVertex
    {
        glm::vec2 XZ;
    };

    const int grid = m_Terrain ? m_Terrain->GetGridSize() : 256;
    const int n = grid + 1;

    std::vector<GridVertex> vertices;
    vertices.reserve(n * n);
    for (int z = 0; z < n; ++z)
    {
        for (int x = 0; x < n; ++x)
            vertices.push_back({ glm::vec2((float)x, (float)z) });
    }

    std::vector<uint32_t> indices;
    indices.reserve((size_t)grid * (size_t)grid * 6u);
    for (int z = 0; z < grid; ++z)
    {
        for (int x = 0; x < grid; ++x)
        {
            const uint32_t i0 = (uint32_t)(x + z * n);
            const uint32_t i1 = (uint32_t)(x + 1 + z * n);
            const uint32_t i2 = (uint32_t)(x + 1 + (z + 1) * n);
            const uint32_t i3 = (uint32_t)(x + (z + 1) * n);
            indices.insert(indices.end(), { i0, i1, i2, i0, i2, i3 });
        }
    }

    Ref<VertexArray> vertexArray = VertexArray::Create();
    auto vertexBuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()),
        (uint32_t)(vertices.size() * sizeof(GridVertex)));
    vertexBuffer->SetLayout({
        { ShaderDataType::Float2, "aXZ", false }
    });
    vertexArray->AddVertexBuffer(vertexBuffer);
    vertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(), (uint32_t)indices.size()));
    vertexArray->Unbind();

    m_Geometry = GeometryLibrary::Register(vertexArray);
    m_IndexCount = (uint32_t)indices.size();

    auto mesh = CreateRef<Mesh>();
    mesh->VertexObject = m_Geometry;
    mesh->TraceIndices = indices;
    Meshes.clear();
    Meshes.push_back(mesh);
}

void WaterNode::CreateShader()
{
    auto library = Application::Get().GetShaderLibrary();
    if (library->Exists("WaterNode"))
    {
        m_Shader = library->Get("WaterNode");
        return;
    }

    const std::string vertexSource = R"(
        #version 400 core
        layout(location = 0) in vec2 aXZ;
        uniform mat4 uMvp;
        uniform mat4 uModel;
        uniform sampler2D uHeightMap;
        uniform sampler2D uWaterBump;
        uniform float uTerrainSize;
        uniform float uTime;
        uniform mat4 uView;
        out vec3 vWorldPos;
        out vec3 vNormal;
        out float vTerrainHeight;
        out float vWaterDepth;
        out vec2 vWaterUv;
        out vec4 vClipPos;
        out float vViewDepth;
        out float vWaveheightCorrection;

        vec4 combineWaterNormal(vec3 worldPos)
        {
            vec4 waterNormal = vec4(0.0, 4.0, 0.0, 0.0);
            vec2 tc = worldPos.xz * vec2(7.0) / uTerrainSize;
            vec2 shift = vec2(uTime * 1.5, uTime * 0.75);
            vec2 variance = vec2(1.0, 1.0);
            float texcoordScale = 1.0;
            float heightScale = 1.0;
            float normalScale = 1.0;
            for (int i = 0; i < 5; ++i)
            {
                vec4 t = textureLod(uWaterBump, tc * texcoordScale + shift * 0.03 * variance, 0.0).rbga;
                variance.x *= -1.0;
                waterNormal.xz += (2.0 * t.xz - vec2(1.0)) * normalScale;
                waterNormal.w += (t.w - 0.5) * heightScale;
                texcoordScale *= 1.4;
                heightScale *= 0.65;
                normalScale *= 0.65;
            }
            waterNormal.w *= 1.0;
            return vec4(normalize(waterNormal.xyz), waterNormal.w);
        }

        void main()
        {
            vec2 xz = aXZ;
            vec2 uv = xz / uTerrainSize;
            vec2 sampleUv = vec2(uv.x, 1.0 - uv.y);
            vTerrainHeight = texture(uHeightMap, sampleUv).w;

            vec3 localPos = vec3(xz.x, -0.5, xz.y);
            vec4 waterNormal = combineWaterNormal(localPos);
            float shoreFade = 0.4 + 0.6 * clamp((-vTerrainHeight) / 10.0, 0.0, 1.0);
            waterNormal.xyz = normalize(mix(vec3(0.0, 1.0, 0.0), waterNormal.xyz, shoreFade));
            localPos.y += waterNormal.w * shoreFade;
            localPos.xz -= waterNormal.xz * 0.5 * shoreFade;

            vec3 worldPos = vec3(uModel * vec4(localPos, 1.0));
            vNormal = normalize(mat3(uModel) * waterNormal.xyz);
            vWorldPos = worldPos;
            vWaterDepth = max(0.0, -vTerrainHeight);
            vWaterUv = localPos.xz * vec2(225.0) / uTerrainSize + vec2(uTime * 1.5, uTime * 0.75) * 0.07;
            vClipPos = uMvp * vec4(localPos, 1.0);
            vViewDepth = -(uView * vec4(vWorldPos, 1.0)).z;
            vec4 waveClip = vClipPos;
            vec4 flatClip = uMvp * vec4(localPos.x, -0.8, localPos.z, 1.0);
            vWaveheightCorrection = -0.5 * waveClip.y / waveClip.w + 0.5 * flatClip.y / flatClip.w;
            gl_Position = vClipPos;
        }
    )";

    const std::string fragmentSource = R"(
        #version 400 core
        layout(location = 0) out vec4 color;
        layout(location = 1) out vec4 gPosition;
        layout(location = 2) out vec4 gNormal;
        in vec3 vWorldPos;
        in vec3 vNormal;
        in float vTerrainHeight;
        in float vWaterDepth;
        in vec2 vWaterUv;
        in vec4 vClipPos;
        in float vViewDepth;
        in float vWaveheightCorrection;
        uniform vec3 uCameraPos;
        uniform float uTime;
        uniform sampler2D uWaterBump;
        uniform sampler2D uReflectionTexture;
        uniform sampler2D uRefractionTexture;
        uniform sampler2D uRefractionDepthTexture;
        uniform vec3 uLightPos;
        uniform mat4 uView;
        uniform vec2 uScreenSizeInv;
        uniform float uZNear;
        uniform float uZFar;

        vec3 calculateFogColor(vec3 pixelToLight, vec3 pixelToEye)
        {
            return mix(vec3(0.6, 0.6, 0.7), vec3(1.0, 1.1, 1.4), 0.5 * dot(pixelToLight, -pixelToEye) + 0.5);
        }

        float linearizeDepth(float depth)
        {
            float z = depth * 2.0 - 1.0;
            return (2.0 * uZNear * uZFar) / (uZFar + uZNear - z * (uZFar - uZNear));
        }

        float refractionDepth(vec2 uv)
        {
            return linearizeDepth(textureLod(uRefractionDepthTexture, clamp(uv, vec2(0.0), vec2(1.0)), 0.0).r);
        }

        float conservativeRefractionDepth(vec2 uv)
        {
            vec2 d = 2.0 * uScreenSizeInv;
            float r = textureLod(uRefractionDepthTexture, clamp(uv + vec2(d.x, d.y), vec2(0.0), vec2(1.0)), 0.0).r;
            r = min(r, textureLod(uRefractionDepthTexture, clamp(uv + vec2(d.x, -d.y), vec2(0.0), vec2(1.0)), 0.0).r);
            r = min(r, textureLod(uRefractionDepthTexture, clamp(uv + vec2(-d.x, d.y), vec2(0.0), vec2(1.0)), 0.0).r);
            r = min(r, textureLod(uRefractionDepthTexture, clamp(uv + vec2(-d.x, -d.y), vec2(0.0), vec2(1.0)), 0.0).r);
            return linearizeDepth(r);
        }

        void main()
        {
            if (vTerrainHeight > 1.7)
                discard;

            vec3 n = normalize(vNormal);
            vec2 shift = vec2(uTime * 1.5, uTime * 0.75);
            vec3 micro0 = normalize(2.0 * texture(uWaterBump, vWaterUv - shift * 0.2).gbr - vec3(1.0, -8.0, 1.0));
            vec3 micro1 = normalize(2.0 * texture(uWaterBump, vWaterUv * 0.5 + shift * 0.05).gbr - vec3(1.0, -8.0, 1.0));
            vec3 tangentZ = normalize(cross(vec3(0.0, 0.0, -1.0), n));
            vec3 tangentX = normalize(cross(tangentZ, n));
            n = normalize(mat3(tangentX, n, tangentZ) * normalize(micro0 + micro1));

            vec3 l = normalize(uLightPos - vWorldPos);
            vec3 v = normalize(uCameraPos - vWorldPos);
            vec3 reflectedEyeToPixel = reflect(-v, n);
            float fresnelBase = (1.2 - 1.0) / (1.2 + 1.0);
            float fresnel = clamp(fresnelBase + (1.0 - fresnelBase) * pow(1.0 - max(dot(n, v), 0.0), 4.0), 0.0, 1.0);

            vec4 disturbanceEye = uView * vec4(n.x, 0.0, n.z, 0.0);
            vec2 reflectionDisturbance = vec2(disturbanceEye.x, disturbanceEye.z) * 0.03;
            vec2 refractionDisturbance = vec2(-disturbanceEye.x, disturbanceEye.y) * 0.05 *
                (20.0 / (20.0 + distance(uCameraPos, vWorldPos)));
            reflectionDisturbance.y = max(-0.15, vWaveheightCorrection + reflectionDisturbance.y);

            vec2 screenUv = vClipPos.xy / vClipPos.w * 0.5 + 0.5;
            float waterDepth = refractionDepth(screenUv) - vViewDepth;
            float nondisplacedWaterDepth = waterDepth;
            refractionDisturbance *= min(2.0, max(0.0, waterDepth));
            waterDepth = refractionDepth(screenUv + refractionDisturbance) - vViewDepth;
            float conservativeWaterDepth = conservativeRefractionDepth(screenUv + refractionDisturbance) - vViewDepth;
            if (conservativeWaterDepth < 0.0)
            {
                refractionDisturbance = vec2(0.0);
                waterDepth = nondisplacedWaterDepth;
            }
            waterDepth = max(0.0, waterDepth);

            vec2 reflectionUv = vec2(screenUv.x, 1.0 - screenUv.y) + reflectionDisturbance;
            vec3 reflectionColor = textureLod(uReflectionTexture, clamp(reflectionUv, vec2(0.0), vec2(1.0)), 0.0).rgb;
            vec3 refractionColor = textureLod(uRefractionTexture, clamp(screenUv + refractionDisturbance, vec2(0.0), vec2(1.0)), 0.0).rgb;

            float scatterFactor = 2.5 * max(0.0, vWorldPos.y * 0.25 + 0.25);
            scatterFactor *= pow(max(0.0, dot(normalize(vec3(l.x, 0.0, l.z)), -v)), 2.0);
            scatterFactor *= pow(max(0.0, 1.0 - dot(l, n)), 8.0);
            scatterFactor += 1.5 * 0.2 * max(0.0, vWorldPos.y + 1.0) *
                max(0.0, dot(v, n)) *
                max(0.0, 1.0 - v.y) *
                (300.0 / (300.0 + distance(uCameraPos, vWorldPos)));
            scatterFactor *= 0.1 + 0.9 * clamp(vWaterDepth / 10.0, 0.0, 1.0);

            float specular = fresnel * pow(max(0.0, dot(l, reflectedEyeToPixel)), 1000.0);
            float diffuse = 0.1 + 0.2 * max(0.0, dot(l, n));
            vec3 waterColor = diffuse * vec3(0.1, 0.4, 0.7);
            vec3 fogColor = calculateFogColor(l, v);
            waterColor = mix(fogColor, waterColor, min(1.0, exp(-distance(uCameraPos, vWorldPos) / 700.0)));

            fresnel *= min(1.0, waterDepth * 5.0);
            refractionColor = mix(waterColor, refractionColor, min(1.0, exp(-waterDepth / 8.0)));
            vec3 outColor = mix(refractionColor, reflectionColor, fresnel);
            outColor += vec3(1.0) * 350.0 * specular * fresnel;
            outColor += vec3(0.3, 0.7, 0.6) * scatterFactor;

            color = vec4(outColor, 1.0);
            gPosition = vec4(vWorldPos, 1.0);
            gNormal = vec4(n, 1.0);
        }
    )";

    m_Shader = library->Load("WaterNode", vertexSource, fragmentSource);
}

void WaterNode::LoadTextures()
{
    m_WaterBumpTexture = TextureLibrary::LoadTexture(IslandTexturePath("water_bump.dds"));
}

void WaterNode::EnsureSceneFramebuffers(uint32_t width, uint32_t height)
{
#ifdef G_OPENGL
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (m_ReflectionFBO && m_SceneTextureWidth == width && m_SceneTextureHeight == height)
        return;

    DestroySceneFramebuffers();
    m_SceneTextureWidth = width;
    m_SceneTextureHeight = height;

    auto createTarget = [&](uint32_t& framebuffer, uint32_t& colorTexture, uint32_t& depthTexture) {
        glCreateFramebuffers(1, &framebuffer);
        glCreateTextures(GL_TEXTURE_2D, 1, &colorTexture);
        glTextureStorage2D(colorTexture, 1, GL_RGBA8, width, height);
        glTextureParameteri(colorTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(colorTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(colorTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(colorTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glCreateTextures(GL_TEXTURE_2D, 1, &depthTexture);
        glTextureStorage2D(depthTexture, 1, GL_DEPTH_COMPONENT24, width, height);
        glTextureParameteri(depthTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(depthTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(depthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0, colorTexture, 0);
        glNamedFramebufferTexture(framebuffer, GL_DEPTH_ATTACHMENT, depthTexture, 0);
        glNamedFramebufferDrawBuffer(framebuffer, GL_COLOR_ATTACHMENT0);
        if (glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            ERROR("Water scene framebuffer is incomplete!");
    };

    createTarget(m_ReflectionFBO, m_ReflectionColorTexture, m_ReflectionDepthTexture);
    createTarget(m_RefractionFBO, m_RefractionColorTexture, m_RefractionDepthTexture);
#else
    (void)width;
    (void)height;
#endif
}

void WaterNode::DestroySceneFramebuffers()
{
#ifdef G_OPENGL
    if (m_ReflectionFBO) glDeleteFramebuffers(1, &m_ReflectionFBO);
    if (m_ReflectionColorTexture) glDeleteTextures(1, &m_ReflectionColorTexture);
    if (m_ReflectionDepthTexture) glDeleteTextures(1, &m_ReflectionDepthTexture);
    if (m_RefractionFBO) glDeleteFramebuffers(1, &m_RefractionFBO);
    if (m_RefractionColorTexture) glDeleteTextures(1, &m_RefractionColorTexture);
    if (m_RefractionDepthTexture) glDeleteTextures(1, &m_RefractionDepthTexture);
#endif
    m_ReflectionFBO = 0;
    m_ReflectionColorTexture = 0;
    m_ReflectionDepthTexture = 0;
    m_RefractionFBO = 0;
    m_RefractionColorTexture = 0;
    m_RefractionDepthTexture = 0;
    m_SceneTextureWidth = 0;
    m_SceneTextureHeight = 0;
}

void WaterNode::RenderSceneTexture(const Scene& scene, const glm::mat4& view, const glm::mat4& projection,
    uint32_t framebuffer, uint32_t width, uint32_t height, const glm::vec4& clipPlane)
{
#ifdef G_OPENGL
    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClearColor(0.62f, 0.74f, 0.86f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    for (const auto& entry : scene.GetObjects())
    {
        if (!entry.Visible || !entry.Object || entry.Object.get() == this || entry.Object->Opacity < 0.999f)
            continue;

        Ref<TerrainHeightMap> terrain = std::dynamic_pointer_cast<TerrainHeightMap>(entry.Object);
        if (terrain)
            terrain->DrawWithClip(view, projection, clipPlane);
        else
            entry.Object->Draw(view, projection, false);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
#else
    (void)scene;
    (void)view;
    (void)projection;
    (void)framebuffer;
    (void)width;
    (void)height;
    (void)clipPlane;
#endif
}

void WaterNode::PrepareSceneTextures(const Scene& scene, const Camera& camera, const glm::vec2& viewportSize)
{
#ifdef G_OPENGL
    const uint32_t width = std::max(1u, (uint32_t)viewportSize.x);
    const uint32_t height = std::max(1u, (uint32_t)viewportSize.y);
    EnsureSceneFramebuffers(width, height);
    if (!m_ReflectionFBO || !m_RefractionFBO)
        return;

    m_ZNear = camera.getNearPlane();
    m_ZFar = camera.getFarPlane();
    const glm::mat4 projection = camera.GetProjectionMatrix();
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 invView = glm::inverse(view);
    const glm::vec3 cameraPos(invView[3]);
    const glm::vec3 cameraForward = glm::normalize(glm::vec3(invView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    const glm::vec3 cameraTarget = cameraPos + cameraForward;
    const glm::vec3 reflectionEye(cameraPos.x, -cameraPos.y + 1.0f, cameraPos.z);
    const glm::vec3 reflectionTarget(cameraTarget.x, -cameraTarget.y + 1.0f, cameraTarget.z);
    const glm::mat4 reflectionView = glm::lookAt(reflectionEye, reflectionTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    RenderSceneTexture(scene, reflectionView, projection, m_ReflectionFBO, width, height,
        glm::vec4(0.0f, 1.0f, 0.0f, 0.6f));
    RenderSceneTexture(scene, view, projection, m_RefractionFBO, width, height,
        glm::vec4(0.0f, 1.0f, 0.0f, -std::numeric_limits<float>::max()));
#else
    (void)scene;
    (void)camera;
    (void)viewportSize;
#endif
}

void WaterNode::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass || !m_Shader)
        return;

    Ref<VertexArray> vertexArray = GeometryLibrary::Resolve(m_Geometry);
    if (!vertexArray)
        return;

    const float time = CurrentSeconds();
    m_Shader->Bind();
    const glm::mat4 model = Transfm.GetMatrix();
    m_Shader->SetMat4("uMvp", proj * view * model);
    m_Shader->SetMat4("uModel", model);
    m_Shader->SetMat4("uView", view);
    m_Shader->SetFloat3("uCameraPos", glm::vec3(glm::inverse(view)[3]));
    m_Shader->SetFloat3("uLightPos", kIslandLightPosition);
    m_Shader->SetFloat("uTerrainSize", (float)(m_Terrain ? m_Terrain->GetGridSize() : 512));
    m_Shader->SetFloat("uTime", time);
    m_Shader->SetInt("uHeightMap", 0);
    m_Shader->SetInt("uWaterBump", 10);
    m_Shader->SetInt("uReflectionTexture", 11);
    m_Shader->SetInt("uRefractionTexture", 12);
    m_Shader->SetInt("uRefractionDepthTexture", 13);
    m_Shader->SetFloat2("uScreenSizeInv", glm::vec2(
        m_SceneTextureWidth > 0 ? 1.0f / (float)m_SceneTextureWidth : 1.0f,
        m_SceneTextureHeight > 0 ? 1.0f / (float)m_SceneTextureHeight : 1.0f));
    m_Shader->SetFloat("uZNear", m_ZNear);
    m_Shader->SetFloat("uZFar", m_ZFar);
#ifdef G_OPENGL
    glBindTextureUnit(0, m_Terrain ? m_Terrain->GetNormalHeightTexture() : 0);
#endif
    if (Ref<Texture> texture = TextureLibrary::Resolve(m_WaterBumpTexture))
        texture->Bind(10);
#ifdef G_OPENGL
    glBindTextureUnit(11, m_ReflectionColorTexture);
    glBindTextureUnit(12, m_RefractionColorTexture);
    glBindTextureUnit(13, m_RefractionDepthTexture);
#endif
    RenderCommand::DrawIndexed(vertexArray, m_IndexCount);
}

void WaterNode::UpdateBoundingSphere()
{
    Bounds.Center = glm::vec3(m_TerrainSize * 0.5f, -0.5f, m_TerrainSize * 0.5f);
    Bounds.Radius = std::sqrt(m_TerrainSize * m_TerrainSize * 0.5f + 4.0f);
    Bounds.Valid = true;
}
