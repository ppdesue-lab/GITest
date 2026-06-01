#include "stdsfx.h"
#include "PathTracer.h"

#include <Camera/Camera.h>
#include <Scene.h>
#include <Renderer/Material.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glad/glad.h>
#include <stb_image.h>

#include <cstring>
#include <limits>
#include <unordered_map>

#define TINYBVH_IMPLEMENTATION
#include <tiny_bvh.h>
#define TINY_OCL_IMPLEMENTATION
#include <tiny_ocl.h>

namespace
{
struct TraceMaterial
{
    glm::vec4 DiffuseRoughness;
    glm::vec4 SpecularMetallic;
    glm::vec4 TextureInfo;
};

void HashBytes(size_t& hash, const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= static_cast<size_t>(1099511628211ull);
    }
}

glm::vec4 MakePoint(const glm::mat4& transform, const glm::vec3& position)
{
    const glm::vec4 transformed = transform * glm::vec4(position, 1.0f);
    return glm::vec4(glm::vec3(transformed) / transformed.w, 0.0f);
}
}

struct PathTracer::Impl
{
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t OutputTexture = 0;
    uint32_t SampleCount = 0;
    uint32_t TriangleCount = 0;
    bool Initialized = false;
    bool SceneDirty = true;
    size_t SceneSignature = 0;
    glm::mat4 LastView = glm::mat4(0.0f);
    glm::mat4 LastProjection = glm::mat4(0.0f);
    std::string EnvironmentPath;
    std::string Status = "Select Rendering to start OpenCL path tracing";

    tinybvh::BVH_GPU BVH;
    std::vector<tinybvh::bvhvec4> Triangles;
    std::vector<glm::vec4> UVs;
    std::vector<TraceMaterial> Materials;
    std::vector<glm::vec4> Environment;
    std::vector<glm::vec4> DiffuseTexels;
    int EnvironmentWidth = 1;
    int EnvironmentHeight = 1;

    Scope<tinyocl::Kernel> Kernel;
    Scope<tinyocl::Buffer> Nodes;
    Scope<tinyocl::Buffer> Indices;
    Scope<tinyocl::Buffer> TriangleData;
    Scope<tinyocl::Buffer> UVData;
    Scope<tinyocl::Buffer> MaterialData;
    Scope<tinyocl::Buffer> DiffuseTextureData;
    Scope<tinyocl::Buffer> EnvironmentData;
    Scope<tinyocl::Buffer> Accumulation;
    Scope<tinyocl::Buffer> Pixels;
    Scope<tinyocl::Buffer> RawRadiance;
    Scope<tinyocl::Buffer> Positions;
    Scope<tinyocl::Buffer> Normals;
    Scope<tinyocl::Buffer> Albedos;

    void CreateOutputTexture()
    {
        if (OutputTexture)
            glDeleteTextures(1, &OutputTexture);
        glCreateTextures(GL_TEXTURE_2D, 1, &OutputTexture);
        glTextureStorage2D(OutputTexture, 1, GL_RGBA16F, Width, Height);
        glTextureParameteri(OutputTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(OutputTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(OutputTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(OutputTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void CreateFrameBuffers()
    {
        if (!Initialized)
            return;
        const uint32_t byteCount = Width * Height * static_cast<uint32_t>(sizeof(glm::vec4));
        Accumulation = CreateScope<tinyocl::Buffer>(byteCount);
        Pixels = CreateScope<tinyocl::Buffer>(byteCount);
        RawRadiance = CreateScope<tinyocl::Buffer>(byteCount);
        Positions = CreateScope<tinyocl::Buffer>(byteCount);
        Normals = CreateScope<tinyocl::Buffer>(byteCount);
        Albedos = CreateScope<tinyocl::Buffer>(byteCount);
        Accumulation->Clear();
        RawRadiance->Clear();
        Positions->Clear();
        Normals->Clear();
        Albedos->Clear();
        SampleCount = 0;
    }

    bool Initialize()
    {
        if (Initialized)
            return true;
        const std::string kernelPath = GetFilePath("../data/shaders/pathtracer.cl");
        if (kernelPath.empty())
        {
            Status = "OpenCL path tracing kernel was not found";
            return false;
        }

        int channels = 0;
        float* pixels = nullptr;
        if (!EnvironmentPath.empty())
        {
            stbi_set_flip_vertically_on_load(false);
            pixels = stbi_loadf(EnvironmentPath.c_str(), &EnvironmentWidth, &EnvironmentHeight, &channels, 3);
        }
        if (pixels)
        {
            Environment.resize(static_cast<size_t>(EnvironmentWidth) * EnvironmentHeight);
            for (size_t i = 0; i < Environment.size(); ++i)
                Environment[i] = glm::vec4(pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2], 1.0f);
            stbi_image_free(pixels);
        }
        else
        {
            EnvironmentWidth = EnvironmentHeight = 1;
            Environment.assign(1, glm::vec4(0.08f, 0.12f, 0.22f, 1.0f));
            WARN("PathTracer environment HDR unavailable, using fallback sky: {}", EnvironmentPath);
        }

        Kernel = CreateScope<tinyocl::Kernel>(kernelPath.c_str(), "PathTrace");
        EnvironmentData = CreateScope<tinyocl::Buffer>(
            static_cast<uint32_t>(Environment.size() * sizeof(glm::vec4)), Environment.data(), tinyocl::Buffer::READONLY);
        EnvironmentData->CopyToDevice();
        Initialized = true;
        CreateFrameBuffers();
        Status = "OpenCL ready";
        return true;
    }

    size_t ComputeSceneSignature(const Scene& scene) const
    {
        size_t hash = static_cast<size_t>(1469598103934665603ull);
        for (const Scene::Entry& entry : scene.GetObjects())
        {
            HashBytes(hash, &entry.Visible, sizeof(entry.Visible));
            if (!entry.Object)
                continue;
            const glm::mat4 objectTransform = entry.Object->Transfm.GetMatrix();
            HashBytes(hash, &objectTransform[0][0], sizeof(glm::mat4));
            for (const Ref<Mesh>& mesh : entry.Object->Meshes)
            {
                const glm::mat4 meshTransform = mesh->Transfm.GetMatrix();
                HashBytes(hash, &meshTransform[0][0], sizeof(glm::mat4));
                const size_t vertexCount = mesh->TraceVertices.size();
                const size_t indexCount = mesh->TraceIndices.size();
                HashBytes(hash, &vertexCount, sizeof(vertexCount));
                HashBytes(hash, &indexCount, sizeof(indexCount));
                if (Ref<MaterialPBR> pbr = std::dynamic_pointer_cast<MaterialPBR>(mesh->Mat))
                {
                    HashBytes(hash, &pbr->Albedo[0], sizeof(glm::vec3));
                    HashBytes(hash, &pbr->Metallic, sizeof(float));
                    HashBytes(hash, &pbr->Roughness, sizeof(float));
                }
                if (Ref<ToonMaterial> toon = std::dynamic_pointer_cast<ToonMaterial>(mesh->Mat))
                {
                    HashBytes(hash, &toon->Diffuse[0], sizeof(glm::vec3));
                    HashBytes(hash, &toon->Specular[0], sizeof(glm::vec3));
                    HashBytes(hash, &toon->SpecularPower, sizeof(float));
                }
            }
        }
        return hash;
    }

    TraceMaterial GetMaterial(const Ref<Material>& material) const
    {
        TraceMaterial result = { glm::vec4(0.75f, 0.75f, 0.75f, 0.55f),
            glm::vec4(0.04f, 0.04f, 0.04f, 0.0f), glm::vec4(0.0f) };
        if (Ref<MaterialPBR> pbr = std::dynamic_pointer_cast<MaterialPBR>(material))
        {
            result.DiffuseRoughness = glm::vec4(pbr->Albedo, glm::clamp(pbr->Roughness, 0.04f, 1.0f));
            result.SpecularMetallic = glm::vec4(
                glm::mix(glm::vec3(0.04f), pbr->Albedo, pbr->Metallic), pbr->Metallic);
        }
        else if (Ref<ToonMaterial> toon = std::dynamic_pointer_cast<ToonMaterial>(material))
        {
            const float roughness = glm::clamp(1.0f - toon->SpecularPower / 128.0f, 0.08f, 0.95f);
            result.DiffuseRoughness = glm::vec4(toon->Diffuse, roughness);
            result.SpecularMetallic = glm::vec4(glm::max(toon->Specular, glm::vec3(0.04f)), 0.0f);
        }
        return result;
    }

    Ref<Texture> GetDiffuseTexture(const Ref<Material>& material) const
    {
        if (Ref<MaterialPBR> pbr = std::dynamic_pointer_cast<MaterialPBR>(material))
            return TextureLibrary::Resolve(pbr->AlbedoMap);
        if (Ref<ToonMaterial> toon = std::dynamic_pointer_cast<ToonMaterial>(material))
            return TextureLibrary::Resolve(toon->MainTexture);
        return nullptr;
    }

    glm::vec4 AddDiffuseTexture(const Ref<Texture>& texture,
        std::unordered_map<const Texture*, glm::vec4>& textureTable)
    {
        if (!texture || !texture->SourceImage || !texture->SourceImage->Data)
            return glm::vec4(0.0f);
        const auto found = textureTable.find(texture.get());
        if (found != textureTable.end())
            return found->second;

        const Ref<Image>& image = texture->SourceImage;
        if (image->Type != PixelType::BYTE || image->Channel < 3)
            return glm::vec4(0.0f);
        const uint32_t offset = static_cast<uint32_t>(DiffuseTexels.size());
        DiffuseTexels.reserve(DiffuseTexels.size() + static_cast<size_t>(image->Width) * image->Height);
        for (int i = 0; i < image->Width * image->Height; ++i)
        {
            const unsigned char* pixel = image->Data + static_cast<size_t>(i) * image->Channel;
            DiffuseTexels.emplace_back(pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, 1.0f);
        }
        const glm::vec4 info(static_cast<float>(offset), static_cast<float>(image->Width),
            static_cast<float>(image->Height), 1.0f);
        textureTable[texture.get()] = info;
        return info;
    }

    void PackScene(const Scene& scene)
    {
        Triangles.clear();
        UVs.clear();
        Materials.clear();
        DiffuseTexels.clear();
        std::unordered_map<const Texture*, glm::vec4> textureTable;
        for (const Scene::Entry& entry : scene.GetObjects())
        {
            if (!entry.Visible || !entry.Object)
                continue;
            for (const Ref<Mesh>& mesh : entry.Object->Meshes)
            {
                if (!mesh || mesh->TraceIndices.empty())
                    continue;
                const glm::mat4 transform = entry.Object->Transfm.GetMatrix() * mesh->Transfm.GetMatrix();
                TraceMaterial material = GetMaterial(mesh->Mat);
                material.TextureInfo = AddDiffuseTexture(GetDiffuseTexture(mesh->Mat), textureTable);
                for (size_t i = 0; i + 2 < mesh->TraceIndices.size(); i += 3)
                {
                    const uint32_t ia = mesh->TraceIndices[i];
                    const uint32_t ib = mesh->TraceIndices[i + 1];
                    const uint32_t ic = mesh->TraceIndices[i + 2];
                    if (ia >= mesh->TraceVertices.size() || ib >= mesh->TraceVertices.size() || ic >= mesh->TraceVertices.size())
                        continue;
                    const glm::vec4 a = MakePoint(transform, mesh->TraceVertices[ia].Position);
                    const glm::vec4 b = MakePoint(transform, mesh->TraceVertices[ib].Position);
                    const glm::vec4 c = MakePoint(transform, mesh->TraceVertices[ic].Position);
                    Triangles.emplace_back(a.x, a.y, a.z, 0.0f);
                    Triangles.emplace_back(b.x, b.y, b.z, 0.0f);
                    Triangles.emplace_back(c.x, c.y, c.z, 0.0f);
                    UVs.emplace_back(mesh->TraceVertices[ia].TexCoord, 0.0f, 0.0f);
                    UVs.emplace_back(mesh->TraceVertices[ib].TexCoord, 0.0f, 0.0f);
                    UVs.emplace_back(mesh->TraceVertices[ic].TexCoord, 0.0f, 0.0f);
                    Materials.push_back(material);
                }
            }
        }

        TriangleCount = static_cast<uint32_t>(Materials.size());
        if (Triangles.empty())
        {
            Triangles = { tinybvh::bvhvec4(0.0f, 0.0f, -1.0e8f, 0.0f),
                tinybvh::bvhvec4(0.0f, 0.0f, -1.0e8f, 0.0f),
                tinybvh::bvhvec4(0.0f, 0.0f, -1.0e8f, 0.0f) };
            UVs.assign(3, glm::vec4(0.0f));
            Materials.push_back(GetMaterial(nullptr));
        }
        if (DiffuseTexels.empty())
            DiffuseTexels.assign(1, glm::vec4(1.0f));

        const uint32_t buildTriangleCount = static_cast<uint32_t>(Materials.size());
        BVH.Build(Triangles.data(), buildTriangleCount);
        Nodes = CreateScope<tinyocl::Buffer>(BVH.usedNodes * static_cast<uint32_t>(sizeof(tinybvh::BVH_GPU::BVHNode)),
            BVH.bvhNode, tinyocl::Buffer::READONLY);
        Indices = CreateScope<tinyocl::Buffer>(BVH.idxCount * static_cast<uint32_t>(sizeof(uint32_t)),
            BVH.bvh.primIdx, tinyocl::Buffer::READONLY);
        TriangleData = CreateScope<tinyocl::Buffer>(static_cast<uint32_t>(Triangles.size() * sizeof(tinybvh::bvhvec4)),
            Triangles.data(), tinyocl::Buffer::READONLY);
        UVData = CreateScope<tinyocl::Buffer>(static_cast<uint32_t>(UVs.size() * sizeof(glm::vec4)),
            UVs.data(), tinyocl::Buffer::READONLY);
        MaterialData = CreateScope<tinyocl::Buffer>(static_cast<uint32_t>(Materials.size() * sizeof(TraceMaterial)),
            Materials.data(), tinyocl::Buffer::READONLY);
        DiffuseTextureData = CreateScope<tinyocl::Buffer>(
            static_cast<uint32_t>(DiffuseTexels.size() * sizeof(glm::vec4)), DiffuseTexels.data(), tinyocl::Buffer::READONLY);
        Nodes->CopyToDevice();
        Indices->CopyToDevice();
        TriangleData->CopyToDevice();
        UVData->CopyToDevice();
        MaterialData->CopyToDevice();
        DiffuseTextureData->CopyToDevice();
        Accumulation->Clear();
        SampleCount = 0;
        Status = "Tracing " + std::to_string(TriangleCount) + " triangles";
    }
};

PathTracer::PathTracer(uint32_t width, uint32_t height, const std::string& environmentPath)
    : m_Impl(CreateScope<Impl>())
{
    m_Impl->Width = std::max(width, 1u);
    m_Impl->Height = std::max(height, 1u);
    m_Impl->EnvironmentPath = environmentPath;
    m_Impl->CreateOutputTexture();
}

PathTracer::~PathTracer()
{
    if (m_Impl && m_Impl->OutputTexture)
        glDeleteTextures(1, &m_Impl->OutputTexture);
}

void PathTracer::Resize(uint32_t width, uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (width == m_Impl->Width && height == m_Impl->Height)
        return;
    m_Impl->Width = width;
    m_Impl->Height = height;
    m_Impl->CreateOutputTexture();
    m_Impl->CreateFrameBuffers();
}

void PathTracer::Render(const Scene& scene, const Camera& camera)
{
    if (!m_Impl->Initialize())
        return;

    const size_t sceneSignature = m_Impl->ComputeSceneSignature(scene);
    if (m_Impl->SceneDirty || sceneSignature != m_Impl->SceneSignature)
    {
        m_Impl->PackScene(scene);
        m_Impl->SceneDirty = false;
        m_Impl->SceneSignature = sceneSignature;
    }

    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 projection = camera.GetProjectionMatrix();
    if (std::memcmp(&view[0][0], &m_Impl->LastView[0][0], sizeof(glm::mat4)) != 0 ||
        std::memcmp(&projection[0][0], &m_Impl->LastProjection[0][0], sizeof(glm::mat4)) != 0)
    {
        m_Impl->Accumulation->Clear();
        m_Impl->SampleCount = 0;
        m_Impl->LastView = view;
        m_Impl->LastProjection = projection;
    }

    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    const glm::vec3 eye3 = glm::vec3(glm::inverse(view)[3]);
    auto cornerRay = [&](float x, float y)
    {
        glm::vec4 point = inverseViewProjection * glm::vec4(x, y, 1.0f, 1.0f);
        point /= point.w;
        return glm::vec4(glm::normalize(glm::vec3(point) - eye3), 0.0f);
    };
    const glm::vec4 eye(eye3, 0.0f);
    const glm::vec4 bottomLeft = cornerRay(-1.0f, -1.0f);
    const glm::vec4 bottomRight = cornerRay(1.0f, -1.0f);
    const glm::vec4 topLeft = cornerRay(-1.0f, 1.0f);

    m_Impl->Kernel->SetArguments(m_Impl->Nodes.get(), m_Impl->Indices.get(), m_Impl->TriangleData.get(), m_Impl->UVData.get(),
        m_Impl->MaterialData.get(), m_Impl->DiffuseTextureData.get(), m_Impl->EnvironmentData.get(), m_Impl->EnvironmentWidth, m_Impl->EnvironmentHeight,
        m_Impl->TriangleCount, eye, bottomLeft, bottomRight, topLeft, m_Impl->Width, m_Impl->Height,
        m_Impl->SampleCount, m_Impl->Accumulation.get(), m_Impl->Pixels.get(),
        m_Impl->RawRadiance.get(), m_Impl->Positions.get(), m_Impl->Normals.get(), m_Impl->Albedos.get());
    m_Impl->Kernel->Run(static_cast<size_t>(m_Impl->Width) * m_Impl->Height);
    m_Impl->Pixels->CopyFromDevice();
    m_Impl->RawRadiance->CopyFromDevice();
    m_Impl->Positions->CopyFromDevice();
    m_Impl->Normals->CopyFromDevice();
    m_Impl->Albedos->CopyFromDevice();
    glTextureSubImage2D(m_Impl->OutputTexture, 0, 0, 0, m_Impl->Width, m_Impl->Height, GL_RGBA, GL_FLOAT,
        m_Impl->Pixels->GetHostPtr());
    ++m_Impl->SampleCount;
}

void PathTracer::MarkSceneDirty()
{
    m_Impl->SceneDirty = true;
}

void PathTracer::ResetAccumulation()
{
    if (m_Impl->Accumulation)
        m_Impl->Accumulation->Clear();
    m_Impl->SampleCount = 0;
}

uint64_t PathTracer::GetOutputTexture() const
{
    return m_Impl->OutputTexture;
}

const glm::vec4* PathTracer::GetRawRadianceBuffer() const
{
    return m_Impl->RawRadiance ? reinterpret_cast<const glm::vec4*>(m_Impl->RawRadiance->GetHostPtr()) : nullptr;
}

const glm::vec4* PathTracer::GetPositionBuffer() const
{
    return m_Impl->Positions ? reinterpret_cast<const glm::vec4*>(m_Impl->Positions->GetHostPtr()) : nullptr;
}

const glm::vec4* PathTracer::GetNormalBuffer() const
{
    return m_Impl->Normals ? reinterpret_cast<const glm::vec4*>(m_Impl->Normals->GetHostPtr()) : nullptr;
}

const glm::vec4* PathTracer::GetAlbedoBuffer() const
{
    return m_Impl->Albedos ? reinterpret_cast<const glm::vec4*>(m_Impl->Albedos->GetHostPtr()) : nullptr;
}

uint32_t PathTracer::GetSampleCount() const
{
    return m_Impl->SampleCount;
}

uint32_t PathTracer::GetTriangleCount() const
{
    return m_Impl->TriangleCount;
}

const std::string& PathTracer::GetStatus() const
{
    return m_Impl->Status;
}
