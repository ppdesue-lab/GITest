#include "stdsfx.h"
#include "Renderer/VolumeObject.h"

#include <Renderer/Buffer.h>
#include <Renderer/RenderCommand.h>
#include <Renderer/Shader.h>
#include <Renderer/VertexArray.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#ifdef G_OPENGL
#include <glad/glad.h>
#endif

namespace
{
constexpr glm::vec3 kVolumeBoundsMin(-0.5f);
constexpr glm::vec3 kVolumeBoundsMax(0.5f);

struct TransferNode
{
    float Value;
    glm::vec4 Color;
};

float NormalizeNodeValue(float value)
{
    constexpr float minValue = -1024.0f;
    constexpr float maxValue = 3071.0f;
    return glm::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

float LerpScalarNodes(const std::vector<std::pair<float, float>>& nodes, float t)
{
    if (nodes.empty())
        return 0.0f;
    if (t <= nodes.front().first)
        return nodes.front().second;
    if (t >= nodes.back().first)
        return nodes.back().second;

    for (size_t i = 1; i < nodes.size(); ++i)
    {
        if (t <= nodes[i].first)
        {
            const auto& previous = nodes[i - 1];
            const auto& current = nodes[i];
            const float span = std::max(current.first - previous.first, 0.000001f);
            const float localT = (t - previous.first) / span;
            return glm::mix(previous.second, current.second, localT);
        }
    }
    return nodes.back().second;
}

glm::vec3 LerpColorNodes(const std::vector<std::pair<float, glm::vec3>>& nodes, float t)
{
    if (nodes.empty())
        return glm::vec3(1.0f);
    if (t <= nodes.front().first)
        return nodes.front().second;
    if (t >= nodes.back().first)
        return nodes.back().second;

    for (size_t i = 1; i < nodes.size(); ++i)
    {
        if (t <= nodes[i].first)
        {
            const auto& previous = nodes[i - 1];
            const auto& current = nodes[i];
            const float span = std::max(current.first - previous.first, 0.000001f);
            const float localT = (t - previous.first) / span;
            return glm::mix(previous.second, current.second, localT);
        }
    }
    return nodes.back().second;
}

const char* VolumeVertexShader()
{
    return R"(
#version 330 core
layout(location = 0) in vec3 a_Position;

uniform mat4 u_WorldViewProjectionMatrix;

void main()
{
    gl_Position = u_WorldViewProjectionMatrix * vec4(a_Position, 1.0);
}
)";
}

const char* VolumeFragmentShader()
{
    return R"(
#version 330 core
layout(location = 0) out vec4 color;

uniform mat4 u_InvWorldViewProjectionMatrix;
uniform mat4 u_NormalMatrix;
uniform vec3 u_BoundingBoxMin;
uniform vec3 u_BoundingBoxMax;
uniform vec2 u_InvRenderTargetDim;
uniform float u_StepSize;
uniform float u_Density;
uniform float u_Exposure;
uniform int u_StepCount;
uniform int u_FrameIndex;
uniform sampler3D u_TextureVolume;
uniform sampler1D u_TextureOpacityTF;
uniform sampler1D u_TextureDiffuseTF;
uniform sampler1D u_TextureSpecularTF;
uniform sampler1D u_TextureRoughnessTF;
uniform sampler2D u_TexturePreviousAccumulation;

const float PI = 3.14159265;

struct Ray
{
    vec3 Origin;
    vec3 Direction;
    float Min;
    float Max;
};

uint pcgHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed >> 8u) * (1.0 / 16777216.0);
}

uint initRng(ivec2 pixel)
{
    return uint(u_FrameIndex) + pcgHash((uint(pixel.x) << 16u) | uint(pixel.y));
}

Ray CreateCameraRay(vec2 pixel)
{
    vec2 ndc = ((pixel + vec2(0.5)) * u_InvRenderTargetDim) * 2.0 - 1.0;
    vec4 nearPoint = u_InvWorldViewProjectionMatrix * vec4(ndc, 0.0, 1.0);
    vec4 farPoint = u_InvWorldViewProjectionMatrix * vec4(ndc, 1.0, 1.0);
    nearPoint.xyz /= nearPoint.w;
    farPoint.xyz /= farPoint.w;

    Ray ray;
    ray.Origin = nearPoint.xyz;
    ray.Direction = normalize(farPoint.xyz - nearPoint.xyz);
    ray.Min = 0.0;
    ray.Max = distance(farPoint.xyz, nearPoint.xyz);
    return ray;
}

vec2 IntersectBox(Ray ray)
{
    vec3 invDirection = 1.0 / ray.Direction;
    vec3 t0 = (u_BoundingBoxMin - ray.Origin) * invDirection;
    vec3 t1 = (u_BoundingBoxMax - ray.Origin) * invDirection;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    return vec2(max(max(tMin.x, tMin.y), tMin.z), min(min(tMax.x, tMax.y), tMax.z));
}

vec3 GetTexcoord(vec3 position)
{
    return (position - u_BoundingBoxMin) / (u_BoundingBoxMax - u_BoundingBoxMin);
}

float GetIntensity(vec3 position)
{
    return textureLod(u_TextureVolume, GetTexcoord(position), 0.0).r;
}

vec3 GetGradient(vec3 position)
{
    vec3 texcoord = GetTexcoord(position);
    vec3 texel = 1.0 / vec3(textureSize(u_TextureVolume, 0));
    float gx = textureLod(u_TextureVolume, texcoord + vec3(texel.x, 0.0, 0.0), 0.0).r
             - textureLod(u_TextureVolume, texcoord - vec3(texel.x, 0.0, 0.0), 0.0).r;
    float gy = textureLod(u_TextureVolume, texcoord + vec3(0.0, texel.y, 0.0), 0.0).r
             - textureLod(u_TextureVolume, texcoord - vec3(0.0, texel.y, 0.0), 0.0).r;
    float gz = textureLod(u_TextureVolume, texcoord + vec3(0.0, 0.0, texel.z), 0.0).r
             - textureLod(u_TextureVolume, texcoord - vec3(0.0, 0.0, texel.z), 0.0).r;
    vec3 gradient = vec3(gx, gy, gz);
    float len2 = dot(gradient, gradient);
    return len2 > 1.0e-10 ? gradient * inversesqrt(len2) : vec3(0.0, 0.0, 1.0);
}

vec3 Environment(vec3 direction)
{
    vec3 worldDir = normalize((u_NormalMatrix * vec4(direction, 0.0)).xyz);
    float horizon = clamp(worldDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(vec3(0.42, 0.08, 0.08), vec3(0.82, 0.88, 1.0), horizon);
    float rim = pow(max(dot(normalize(worldDir), normalize(vec3(-0.4, 0.7, 0.6))), 0.0), 24.0);
    return sky + vec3(4.0, 4.4, 5.0) * rim;
}

vec3 TangentSpaceSample(vec3 normal, float alpha, inout uint rng)
{
    vec2 xi = vec2(rand(rng), rand(rng));
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt(max(0.0, (1.0 - xi.y) / (1.0 + alpha * alpha * xi.y - xi.y)));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 helper = abs(normal.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(normal, helper));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * cos(phi) * sinTheta + bitangent * sin(phi) * sinTheta + normal * cosTheta);
}

vec3 FresnelSchlick(vec3 f0, float vdotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - vdotH, 5.0);
}

float GgxPartialGeometry(float ndotx, float alpha)
{
    float aa = alpha * alpha;
    return 2.0 * ndotx / max(ndotx + sqrt(aa + (1.0 - aa) * ndotx * ndotx), 1.0e-6);
}

bool RayMarchOcclusion(Ray ray, inout uint rng)
{
    vec2 hit = IntersectBox(ray);
    if (hit.y < hit.x)
        return false;

    float minT = max(hit.x, ray.Min);
    float maxT = min(hit.y, ray.Max);
    float threshold = -log(max(rand(rng), 1.0e-6)) / u_Density;
    float sum = 0.0;
    float t = minT + rand(rng) * u_StepSize;

    for (int i = 0; i < 1024; ++i)
    {
        if (i >= u_StepCount || t >= maxT)
            return false;

        vec3 position = ray.Origin + t * ray.Direction;
        float intensity = GetIntensity(position);
        float opacity = texture(u_TextureOpacityTF, intensity).r;
        sum += u_Density * opacity * u_StepSize;

        if (sum >= threshold)
            return true;
        t += u_StepSize;
    }

    return false;
}

bool SampleScatterEvent(Ray ray, inout uint rng, out vec3 position, out float intensity)
{
    vec2 hit = IntersectBox(ray);
    if (hit.y < hit.x)
        return false;

    float minT = max(hit.x, ray.Min);
    float maxT = min(hit.y, ray.Max);
    float threshold = -log(max(1.0 - rand(rng), 1.0e-6)) / u_Density;
    float sum = 0.0;
    float t = minT + rand(rng) * u_StepSize;

    for (int i = 0; i < 1024; ++i)
    {
        if (i >= u_StepCount || t >= maxT)
            return false;

        position = ray.Origin + t * ray.Direction;
        intensity = GetIntensity(position);
        float opacity = texture(u_TextureOpacityTF, intensity).r;
        sum += u_Density * opacity * u_StepSize;

        if (sum >= threshold)
            return true;
        t += u_StepSize;
    }

    return false;
}

vec3 EstimateRadiance(Ray cameraRay, inout uint rng)
{
    vec3 eventPosition;
    float eventIntensity;
    if (!SampleScatterEvent(cameraRay, rng, eventPosition, eventIntensity))
        return vec3(0.0);

    vec3 normal = GetGradient(eventPosition);
    if (dot(normal, -cameraRay.Direction) < 0.0)
        normal = -normal;

    vec3 view = normalize(-cameraRay.Direction);
    vec3 diffuse = texture(u_TextureDiffuseTF, eventIntensity).rgb;
    vec3 specular = texture(u_TextureSpecularTF, eventIntensity).rgb;
    float roughness = max(texture(u_TextureRoughnessTF, eventIntensity).r, 0.04);
    float alpha = roughness * roughness;

    vec3 halfVector = TangentSpaceSample(normal, alpha, rng);
    vec3 fresnel = FresnelSchlick(specular, clamp(dot(view, halfVector), 0.0, 1.0));

    float pd = length(vec3(1.0) - fresnel);
    float ps = length(fresnel);
    float pdf = ps / max(ps + pd, 1.0e-6);

    Ray lightRay;
    lightRay.Origin = eventPosition + normal * 0.01;
    lightRay.Min = 0.0;
    lightRay.Max = 10000.0;

    vec3 throughput;
    if (rand(rng) < pdf)
    {
        vec3 lightDirection = reflect(-view, halfVector);
        float ndotl = clamp(dot(normal, lightDirection), 0.0, 1.0);
        float ndotv = clamp(dot(normal, view), 0.0, 1.0);
        float ndoth = clamp(dot(normal, halfVector), 0.0, 1.0);
        float vdoth = clamp(dot(view, halfVector), 0.0, 1.0);
        float geometry = GgxPartialGeometry(ndotv, alpha) * GgxPartialGeometry(ndotl, alpha);

        lightRay.Direction = lightDirection;
        throughput = (geometry * fresnel * vdoth) / max(ndotv * ndoth * pdf, 1.0e-6);
    }
    else
    {
        vec3 lightDirection = TangentSpaceSample(normal, 1.0, rng);
        lightRay.Direction = lightDirection;
        throughput = (vec3(1.0) - fresnel) * diffuse / max(1.0 - pdf, 1.0e-6);
    }

    bool occluded = RayMarchOcclusion(lightRay, rng);
    return occluded ? vec3(0.0) : throughput * Environment(lightRay.Direction);
}

vec3 Uncharted2Function(vec3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 ToneMap(vec3 hdr)
{
    const float W = 11.2;
    return Uncharted2Function(hdr) * u_Exposure / Uncharted2Function(vec3(W));
}

void main()
{
    uint rng = initRng(ivec2(gl_FragCoord.xy));
    Ray ray = CreateCameraRay(gl_FragCoord.xy);
    vec3 sampleRadiance = EstimateRadiance(ray, rng);
    vec3 previous = texelFetch(u_TexturePreviousAccumulation, ivec2(gl_FragCoord.xy), 0).rgb;
    float alpha = 1.0 / float(u_FrameIndex + 1);
    vec3 accumulated = mix(previous, sampleRadiance, alpha);
    color = vec4(accumulated, 1.0);
}
)";
}

const char* VolumeCompositeVertexShader()
{
    return R"(
#version 330 core
out vec2 v_Texcoord;

void main()
{
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 position = positions[gl_VertexID];
    v_Texcoord = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";
}

const char* VolumeCompositeFragmentShader()
{
    return R"(
#version 330 core
in vec2 v_Texcoord;
layout(location = 0) out vec4 color;

uniform sampler2D u_TextureAccumulation;
uniform float u_Exposure;

vec3 Uncharted2Function(vec3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 ToneMap(vec3 hdr)
{
    const float W = 11.2;
    return Uncharted2Function(hdr) * u_Exposure / Uncharted2Function(vec3(W));
}

void main()
{
    vec3 mapped = ToneMap(texture(u_TextureAccumulation, v_Texcoord).rgb);
    float alpha = clamp(max(max(mapped.r, mapped.g), mapped.b), 0.0, 1.0);
    //if (alpha <= 0.0001)
    //    discard;
    color = vec4(mapped, alpha);
}
)";
}

bool MatricesClose(const glm::mat4& a, const glm::mat4& b)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(a[column][row] - b[column][row]) > 0.0001f)
                return false;
    return true;
}
}

VolumeObject::VolumeObject(const std::filesystem::path& filepath)
{
    CreateShader();
    CreateCubeGeometry();
    CreateTransferTexture();
    m_Loaded = LoadManixDat(filepath);
    UpdateBoundingSphere();
}

VolumeObject::~VolumeObject()
{
    GeometryLibrary::Release(m_CubeGeometry);
    DestroyGLResources();
}

void VolumeObject::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
    if (transparentPass || !m_Loaded || !m_Shader || !m_CompositeShader)
        return;

    Ref<VertexArray> cube = GeometryLibrary::Resolve(m_CubeGeometry);
    if (!cube)
        return;

    glm::vec2 invRenderTargetDim(1.0f);

#ifdef G_OPENGL
    GLint viewport[4] = { 0, 0, 1, 1 };
    glGetIntegerv(GL_VIEWPORT, viewport);
    invRenderTargetDim = glm::vec2(
        viewport[2] > 0 ? 1.0f / (float)viewport[2] : 1.0f,
        viewport[3] > 0 ? 1.0f / (float)viewport[3] : 1.0f);

    const uint32_t renderWidth = (uint32_t)std::max(viewport[2], 1);
    const uint32_t renderHeight = (uint32_t)std::max(viewport[3], 1);
    EnsureAccumulationResources(renderWidth, renderHeight);

    GLboolean previousBlend = glIsEnabled(GL_BLEND);    
    GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
    GLboolean previousDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousCullFace = GL_BACK;
    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFace);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
#endif

    const glm::mat4 model = Transfm.GetMatrix();
    const glm::mat4 worldViewProjection = proj * view * model;
    const glm::mat4 normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model))));
    const bool transformChanged = !m_HasLastWorldViewProjection ||
        !MatricesClose(m_LastWorldViewProjection, worldViewProjection);
    if (transformChanged)
        ResetAccumulation();
    m_LastWorldViewProjection = worldViewProjection;
    m_HasLastWorldViewProjection = true;

#ifdef G_OPENGL
    const uint32_t dstAccumulation = 1u - m_CurrentAccumulation;
    const uint32_t srcAccumulation = m_CurrentAccumulation;

    glBindFramebuffer(GL_FRAMEBUFFER, m_AccumulationFBO);
    glNamedFramebufferTexture(m_AccumulationFBO, GL_COLOR_ATTACHMENT0, m_AccumulationTextures[dstAccumulation], 0);
    const GLenum accumulationBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &accumulationBuffer);
    glViewport(0, 0, (GLsizei)m_AccumulationWidth, (GLsizei)m_AccumulationHeight);
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glClearBufferfv(GL_COLOR, 0, clearColor);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    m_Shader->Bind();
    m_Shader->SetMat4("u_WorldViewProjectionMatrix", worldViewProjection);
    m_Shader->SetMat4("u_InvWorldViewProjectionMatrix", glm::inverse(worldViewProjection));
    m_Shader->SetMat4("u_NormalMatrix", normalMatrix);
    m_Shader->SetFloat3("u_BoundingBoxMin", kVolumeBoundsMin);
    m_Shader->SetFloat3("u_BoundingBoxMax", kVolumeBoundsMax);
    m_Shader->SetFloat2("u_InvRenderTargetDim", invRenderTargetDim);
    m_Shader->SetFloat("u_StepSize", glm::length(kVolumeBoundsMax - kVolumeBoundsMin) / (float)m_StepCount);
    m_Shader->SetFloat("u_Density", m_Density);
    m_Shader->SetFloat("u_Exposure", m_Exposure);
    m_Shader->SetInt("u_StepCount", m_StepCount);
    m_Shader->SetInt("u_FrameIndex", (int)m_FrameIndex);
    m_Shader->SetInt("u_TextureVolume", 0);
    m_Shader->SetInt("u_TextureOpacityTF", 1);
    m_Shader->SetInt("u_TextureDiffuseTF", 2);
    m_Shader->SetInt("u_TextureSpecularTF", 3);
    m_Shader->SetInt("u_TextureRoughnessTF", 4);
    m_Shader->SetInt("u_TexturePreviousAccumulation", 5);
    glBindTextureUnit(0, m_VolumeTexture);
    glBindTextureUnit(1, m_OpacityTransferTexture);
    glBindTextureUnit(2, m_DiffuseTransferTexture);
    glBindTextureUnit(3, m_SpecularTransferTexture);
    glBindTextureUnit(4, m_RoughnessTransferTexture);
    glBindTextureUnit(5, m_AccumulationTextures[srcAccumulation]);
    RenderCommand::DrawIndexed(cube);
    m_CurrentAccumulation = dstAccumulation;
    ++m_FrameIndex;

    glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_FullscreenVAO);
    m_CompositeShader->Bind();
    m_CompositeShader->SetInt("u_TextureAccumulation", 0);
    m_CompositeShader->SetFloat("u_Exposure", m_Exposure);
    glBindTextureUnit(0, m_AccumulationTextures[m_CurrentAccumulation]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glCullFace(previousCullFace);
    if (!previousCull)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    if (previousDepthTest)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(previousDepthMask);
    if (previousBlend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
#endif
}

void VolumeObject::UpdateBoundingSphere()
{
    Bounds.Center = glm::vec3(0.0f);
    Bounds.Radius = glm::length(kVolumeBoundsMax - kVolumeBoundsMin) * 0.5f;
    Bounds.Valid = true;
}

bool VolumeObject::LoadManixDat(const std::filesystem::path& filepath)
{
#ifndef G_OPENGL
    WARN("Volume rendering is only implemented for the OpenGL backend.");
    return false;
#else
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
    {
        WARN("Failed to open manix volume: {}", filepath.u8string());
        return false;
    }

    uint16_t dimensions[3] = {};
    file.read(reinterpret_cast<char*>(dimensions), sizeof(dimensions));
    if (!file)
    {
        WARN("Failed to read manix dimensions: {}", filepath.u8string());
        return false;
    }

    m_Dimensions = glm::uvec3(dimensions[0], dimensions[1], dimensions[2]);
    const size_t voxelCount = (size_t)m_Dimensions.x * (size_t)m_Dimensions.y * (size_t)m_Dimensions.z;
    if (voxelCount == 0)
    {
        WARN("Manix volume has invalid dimensions.");
        return false;
    }

    std::vector<uint16_t> voxels(voxelCount);
    file.read(reinterpret_cast<char*>(voxels.data()), (std::streamsize)(voxels.size() * sizeof(uint16_t)));
    if (!file)
    {
        WARN("Failed to read manix voxels: {}", filepath.u8string());
        return false;
    }

    constexpr uint16_t minIntensity = 0;
    constexpr uint16_t maxIntensity = 1 << 12;
    for (uint16_t& value : voxels)
    {
        const float normalized = (float)(value - minIntensity) / (float)(maxIntensity - minIntensity);
        value = (uint16_t)std::round(65535.0f * glm::clamp(normalized, 0.0f, 1.0f));
    }

    if (!m_VolumeTexture)
        glCreateTextures(GL_TEXTURE_3D, 1, &m_VolumeTexture);
    const int maxDimension = (int)std::max({ m_Dimensions.x, m_Dimensions.y, m_Dimensions.z });
    const int mipLevels = 1 + (int)std::floor(std::log2((float)std::max(maxDimension, 1)));
    glTextureStorage3D(m_VolumeTexture, mipLevels, GL_R16, m_Dimensions.x, m_Dimensions.y, m_Dimensions.z);
    glTextureSubImage3D(m_VolumeTexture, 0, 0, 0, 0, m_Dimensions.x, m_Dimensions.y, m_Dimensions.z,
        GL_RED, GL_UNSIGNED_SHORT, voxels.data());
    glGenerateTextureMipmap(m_VolumeTexture);
    glTextureParameteri(m_VolumeTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_VolumeTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_VolumeTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_VolumeTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_VolumeTexture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    const glm::vec3 scale(0.488f * (float)m_Dimensions.x, 0.488f * (float)m_Dimensions.y, 0.7f * (float)m_Dimensions.z);
    const float maxScale = std::max({ scale.x, scale.y, scale.z, 0.000001f });
    Transfm.scale = scale / maxScale * 120.0f;
    Transfm.rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    INFO("Loaded manix volume: {} x {} x {}", m_Dimensions.x, m_Dimensions.y, m_Dimensions.z);
    return true;
#endif
}

void VolumeObject::CreateCubeGeometry()
{
    const std::array<glm::vec3, 8> vertices = {
        glm::vec3(-0.5f, -0.5f, -0.5f),
        glm::vec3( 0.5f, -0.5f, -0.5f),
        glm::vec3( 0.5f,  0.5f, -0.5f),
        glm::vec3(-0.5f,  0.5f, -0.5f),
        glm::vec3(-0.5f, -0.5f,  0.5f),
        glm::vec3( 0.5f, -0.5f,  0.5f),
        glm::vec3( 0.5f,  0.5f,  0.5f),
        glm::vec3(-0.5f,  0.5f,  0.5f)
    };
    std::array<uint32_t, 36> indices = {
        0, 1, 2, 2, 3, 0,
        5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4,
        1, 5, 6, 6, 2, 1,
        3, 2, 6, 6, 7, 3,
        4, 5, 1, 1, 0, 4
    };

    Ref<VertexArray> vertexArray = VertexArray::Create();
    Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create((float*)&vertices[0].x, (uint32_t)(vertices.size() * sizeof(glm::vec3)));
    vertexBuffer->SetLayout({ { ShaderDataType::Float3, "a_Position", false } });
    vertexArray->AddVertexBuffer(vertexBuffer);
    Ref<IndexBuffer> indexBuffer = IndexBuffer::Create(indices.data(), (uint32_t)indices.size());
    vertexArray->SetIndexBuffer(indexBuffer);
    vertexArray->Unbind();
    m_CubeGeometry = GeometryLibrary::Register(vertexArray, "ManixVolumeCube");
}

void VolumeObject::CreateTransferTexture()
{
#ifdef G_OPENGL
    const std::vector<std::pair<float, glm::vec3>> colorNodes = {
        { NormalizeNodeValue(-1024.0f), glm::vec3(0.0f, 0.0f, 0.0f) },
        { NormalizeNodeValue(-600.0f), glm::vec3(1.0f, 0.561447f, 0.448113f) },
        { NormalizeNodeValue(-400.0f), glm::vec3(1.0f, 0.546454f, 0.429245f) },
        { NormalizeNodeValue(-100.0f), glm::vec3(1.0f, 0.857894f, 0.599057f) },
        { NormalizeNodeValue(-60.0f), glm::vec3(1.0f, 0.854550f, 0.589623f) },
        { NormalizeNodeValue(40.0f), glm::vec3(0.707547f, 0.0f, 0.0f) },
        { NormalizeNodeValue(80.0f), glm::vec3(1.0f, 0.0f, 0.0f) },
        { NormalizeNodeValue(400.0f), glm::vec3(1.0f, 1.0f, 1.0f) },
        { NormalizeNodeValue(3071.0f), glm::vec3(1.0f, 1.0f, 1.0f) }
    };
    const std::vector<std::pair<float, float>> opacityNodes = {
        { NormalizeNodeValue(-1024.0f), 0.0f },
        { NormalizeNodeValue(-726.619f), 0.0f },
        { NormalizeNodeValue(-709.786f), 0.0f },
        { NormalizeNodeValue(-680.649f), 0.0f },
        { NormalizeNodeValue(53.304f), 0.0f },
        { NormalizeNodeValue(115.025f), 1.0f },
        { NormalizeNodeValue(135.406f), 1.0f },
        { NormalizeNodeValue(277.384f), 1.0f },
        { NormalizeNodeValue(281.271f), 0.943748f },
        { NormalizeNodeValue(286.0f), 1.0f },
        { NormalizeNodeValue(3071.0f), 0.0f }
    };

    const std::vector<std::pair<float, glm::vec3>> specularNodes = {
        { NormalizeNodeValue(-1024.0f), glm::vec3(0.04f, 0.04f, 0.04f) },
        { NormalizeNodeValue(-600.0f), glm::vec3(0.04f, 0.04f, 0.04f) },
        { NormalizeNodeValue(-400.0f), glm::vec3(0.04f, 0.04f, 0.04f) },
        { NormalizeNodeValue(-100.0f), glm::vec3(0.04f, 0.04f, 0.04f) },
        { NormalizeNodeValue(-60.0f), glm::vec3(0.2f, 0.2f, 0.2f) },
        { NormalizeNodeValue(40.0f), glm::vec3(0.04f, 0.04f, 0.04f) },
        { NormalizeNodeValue(80.0f), glm::vec3(0.0f, 0.0f, 0.0f) },
        { NormalizeNodeValue(400.0f), glm::vec3(0.141509f, 0.141509f, 0.141509f) },
        { NormalizeNodeValue(3071.0f), glm::vec3(0.0f, 0.0f, 0.0f) }
    };
    const std::vector<std::pair<float, float>> roughnessNodes = {
        { NormalizeNodeValue(-1024.0f), 0.0f },
        { NormalizeNodeValue(-600.0f), 1.0f },
        { NormalizeNodeValue(-400.0f), 1.0f },
        { NormalizeNodeValue(-100.0f), 0.348f },
        { NormalizeNodeValue(-60.0f), 0.378f },
        { NormalizeNodeValue(40.0f), 0.034f },
        { NormalizeNodeValue(80.0f), 0.021f },
        { NormalizeNodeValue(400.0f), 0.088f },
        { NormalizeNodeValue(3071.0f), 1.0f }
    };

    constexpr uint32_t transferSize = 256;
    std::array<float, transferSize> opacity = {};
    std::array<float, transferSize> roughness = {};
    std::array<glm::vec4, transferSize> diffuse = {};
    std::array<glm::vec4, transferSize> specular = {};
    for (uint32_t i = 0; i < transferSize; ++i)
    {
        const float t = (float)i / (float)(transferSize - 1);
        opacity[i] = LerpScalarNodes(opacityNodes, t);
        roughness[i] = LerpScalarNodes(roughnessNodes, t);
        diffuse[i] = glm::vec4(LerpColorNodes(colorNodes, t), 1.0f);
        specular[i] = glm::vec4(LerpColorNodes(specularNodes, t), 1.0f);
    }

    glCreateTextures(GL_TEXTURE_1D, 1, &m_OpacityTransferTexture);
    glTextureStorage1D(m_OpacityTransferTexture, 1, GL_R32F, transferSize);
    glTextureSubImage1D(m_OpacityTransferTexture, 0, 0, transferSize, GL_RED, GL_FLOAT, opacity.data());
    glTextureParameteri(m_OpacityTransferTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_OpacityTransferTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_OpacityTransferTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_1D, 1, &m_DiffuseTransferTexture);
    glTextureStorage1D(m_DiffuseTransferTexture, 1, GL_RGBA32F, transferSize);
    glTextureSubImage1D(m_DiffuseTransferTexture, 0, 0, transferSize, GL_RGBA, GL_FLOAT, diffuse.data());
    glTextureParameteri(m_DiffuseTransferTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_DiffuseTransferTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_DiffuseTransferTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_1D, 1, &m_SpecularTransferTexture);
    glTextureStorage1D(m_SpecularTransferTexture, 1, GL_RGBA32F, transferSize);
    glTextureSubImage1D(m_SpecularTransferTexture, 0, 0, transferSize, GL_RGBA, GL_FLOAT, specular.data());
    glTextureParameteri(m_SpecularTransferTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_SpecularTransferTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_SpecularTransferTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    glCreateTextures(GL_TEXTURE_1D, 1, &m_RoughnessTransferTexture);
    glTextureStorage1D(m_RoughnessTransferTexture, 1, GL_R32F, transferSize);
    glTextureSubImage1D(m_RoughnessTransferTexture, 0, 0, transferSize, GL_RED, GL_FLOAT, roughness.data());
    glTextureParameteri(m_RoughnessTransferTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_RoughnessTransferTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_RoughnessTransferTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
#endif
}

void VolumeObject::CreateShader()
{
    m_Shader = Shader::Create("ManixVolumeRaymarch", VolumeVertexShader(), VolumeFragmentShader());
    m_CompositeShader = Shader::Create("ManixVolumeComposite", VolumeCompositeVertexShader(), VolumeCompositeFragmentShader());
}

void VolumeObject::EnsureAccumulationResources(uint32_t width, uint32_t height)
{
#ifdef G_OPENGL
    if (m_AccumulationFBO && m_AccumulationWidth == width && m_AccumulationHeight == height)
        return;

    if (!m_AccumulationFBO)
        glCreateFramebuffers(1, &m_AccumulationFBO);
    if (!m_FullscreenVAO)
        glCreateVertexArrays(1, &m_FullscreenVAO);

    if (m_AccumulationTextures[0] || m_AccumulationTextures[1])
        glDeleteTextures(2, m_AccumulationTextures);

    glCreateTextures(GL_TEXTURE_2D, 2, m_AccumulationTextures);
    for (uint32_t texture : m_AccumulationTextures)
    {
        glTextureStorage2D(texture, 1, GL_RGBA32F, width, height);
        glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_AccumulationWidth = width;
    m_AccumulationHeight = height;
    ResetAccumulation();
#endif
}

void VolumeObject::ResetAccumulation()
{
    m_FrameIndex = 0;
    m_CurrentAccumulation = 0;
    m_HasLastWorldViewProjection = false;
#ifdef G_OPENGL
    if (m_AccumulationTextures[0] && m_AccumulationTextures[1])
    {
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        glClearTexImage(m_AccumulationTextures[0], 0, GL_RGBA, GL_FLOAT, clearColor);
        glClearTexImage(m_AccumulationTextures[1], 0, GL_RGBA, GL_FLOAT, clearColor);
    }
#endif
}

void VolumeObject::DestroyGLResources()
{
#ifdef G_OPENGL
    if (m_VolumeTexture)
    {
        glDeleteTextures(1, &m_VolumeTexture);
        m_VolumeTexture = 0;
    }
    if (m_OpacityTransferTexture)
    {
        glDeleteTextures(1, &m_OpacityTransferTexture);
        m_OpacityTransferTexture = 0;
    }
    if (m_DiffuseTransferTexture)
    {
        glDeleteTextures(1, &m_DiffuseTransferTexture);
        m_DiffuseTransferTexture = 0;
    }
    if (m_SpecularTransferTexture)
    {
        glDeleteTextures(1, &m_SpecularTransferTexture);
        m_SpecularTransferTexture = 0;
    }
    if (m_RoughnessTransferTexture)
    {
        glDeleteTextures(1, &m_RoughnessTransferTexture);
        m_RoughnessTransferTexture = 0;
    }
    if (m_AccumulationTextures[0] || m_AccumulationTextures[1])
    {
        glDeleteTextures(2, m_AccumulationTextures);
        m_AccumulationTextures[0] = 0;
        m_AccumulationTextures[1] = 0;
    }
    if (m_AccumulationFBO)
    {
        glDeleteFramebuffers(1, &m_AccumulationFBO);
        m_AccumulationFBO = 0;
    }
    if (m_FullscreenVAO)
    {
        glDeleteVertexArrays(1, &m_FullscreenVAO);
        m_FullscreenVAO = 0;
    }
#endif
}
