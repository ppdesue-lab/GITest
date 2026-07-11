#include "stdsfx.h"
#include "SSAO.h"
#include "ProfileTimer.h"

#include <random>

#ifdef G_OPENGL
#include <glad/glad.h>
#endif
#ifdef G_DX11
#include <Platform/DX11/DX11Context.h>
#include <d3dcompiler.h>
#include <Renderer/Renderer.h>
#endif

SSAO::SSAO(uint32_t width, uint32_t height)
    : m_Width(width), m_Height(height)
{
#ifdef G_OPENGL
    const std::string fullscreenVertex = R"(
        #version 410 core
        out vec2 v_UV;
        void main()
        {
            vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            v_UV = p;
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
    )";
    const std::string aoFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec4 o_AO;
        uniform sampler2D u_Depth;
        uniform sampler2D u_Normal;
        uniform sampler2D u_Noise;
        uniform mat4 u_View;
        uniform mat4 u_Projection;
        uniform mat4 u_InverseProjection;
        uniform vec2 u_Resolution;
        uniform float u_Radius;
        uniform float u_Bias;
        uniform float u_Strength;
        uniform vec3 u_Samples[64];

        vec3 reconstructViewPosition(vec2 uv, float depth)
        {
            vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            vec4 view = u_InverseProjection * clip;
            return view.xyz / view.w;
        }

        void main()
        {
            float centerDepth = texture(u_Depth, v_UV).r;
            if (centerDepth >= 0.999999)
            {
                o_AO = vec4(1.0);
                return;
            }
            vec3 center = reconstructViewPosition(v_UV, centerDepth);
            vec3 normal = normalize(mat3(u_View) * texture(u_Normal, v_UV).xyz);
            vec3 randomVector = normalize(texture(u_Noise, v_UV * u_Resolution / 4.0).xyz);
            vec3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
            vec3 bitangent = cross(normal, tangent);
            mat3 tbn = mat3(tangent, bitangent, normal);
            float occlusion = 0.0;
            for (int i = 0; i < 64; ++i)
            {
                vec3 samplePosition = center + (tbn * u_Samples[i]) * u_Radius;
                vec4 offset = u_Projection * vec4(samplePosition, 1.0);
                offset.xyz /= offset.w;
                offset.xyz = offset.xyz * 0.5 + 0.5;
                vec2 sampleUV = clamp(offset.xy, vec2(0.0), vec2(1.0));
                float sampleDepthValue = texture(u_Depth, sampleUV).r;
                if (sampleDepthValue >= 0.999999)
                    continue;
                float sampleDepth = reconstructViewPosition(sampleUV, sampleDepthValue).z;
                float rangeWeight = smoothstep(0.0, 1.0, u_Radius / max(abs(center.z - sampleDepth), 0.0001));
                occlusion += (sampleDepth >= samplePosition.z + u_Bias ? 1.0 : 0.0) * rangeWeight;
            }
            float ao = pow(clamp(1.0 - occlusion / 64.0, 0.0, 1.0), max(u_Strength, 0.001));
            o_AO = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
        }
    )";
    const std::string compositeFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec4 o_Color;
        uniform sampler2D u_SceneColor;
        uniform sampler2D u_AO;
        uniform int u_DebugMode;
        void main()
        {
            vec4 scene = texture(u_SceneColor, v_UV);
            float ao = texture(u_AO, v_UV).r;
            if (u_DebugMode == 1)
            {
                o_Color = vec4(vec3(ao), 1.0);
                return;
            }
            o_Color = vec4(scene.rgb * mix(0.45, 1.0, ao), scene.a);
        }
    )";
    const std::string blurFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out float o_AO;
        uniform sampler2D u_AO;
        uniform sampler2D u_Depth;
        uniform mat4 u_InverseProjection;
        uniform vec2 u_Resolution;

        vec3 reconstructViewPosition(vec2 uv, float depth)
        {
            vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            vec4 view = u_InverseProjection * clip;
            return view.xyz / view.w;
        }

        void main()
        {
            float centerDepth = texture(u_Depth, v_UV).r;
            if (centerDepth >= 0.999999)
            {
                o_AO = 1.0;
                return;
            }
            vec3 centerPosition = reconstructViewPosition(v_UV, centerDepth);

            vec2 texel = 1.0 / u_Resolution;
            float sum = 0.0;
            float weightSum = 0.0;
            for (int y = -2; y <= 2; ++y)
            {
                for (int x = -2; x <= 2; ++x)
                {
                    vec2 uv = clamp(v_UV + vec2(x, y) * texel, vec2(0.0), vec2(1.0));
                    float sampleDepth = texture(u_Depth, uv).r;
                    if (sampleDepth >= 0.999999)
                        continue;
                    vec3 samplePosition = reconstructViewPosition(uv, sampleDepth);

                    float spatialWeight = exp(-dot(vec2(x, y), vec2(x, y)) / 4.0);
                    float geometryWeight = exp(-length(samplePosition - centerPosition) * 0.4);
                    float weight = spatialWeight * geometryWeight;
                    sum += texture(u_AO, uv).r * weight;
                    weightSum += weight;
                }
            }
            o_AO = sum / max(weightSum, 0.0001);
        }
    )";
    const std::string hbaoFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec2 o_AODepth;
        uniform sampler2D u_Depth;
        uniform sampler2D u_Random;
        uniform mat4 u_InverseProjection;
        uniform vec2 u_FullResolution;
        uniform vec2 u_InvFullResolution;
        uniform vec2 u_FocalLen;
        uniform float u_Radius;
        uniform float u_NegInvR2;
        uniform float u_MaxRadiusPixels;
        uniform float u_TanAngleBias;
        uniform float u_Strength;
        uniform int u_StepSize;
        const float PI = 3.14159265;
        const int NUM_DIRECTIONS = 8;
        const int NUM_STEPS = 6;

        vec3 reconstructViewPosition(vec2 uv, float depth)
        {
            vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            vec4 view = u_InverseProjection * clip;
            view.xyz /= view.w;
            return vec3(view.xy, -view.z);
        }

        float invLength(vec2 v) { return inversesqrt(max(dot(v, v), 0.000001)); }
        float tangent(vec3 P, vec3 S) { return (P.z - S.z) * invLength(S.xy - P.xy); }
        vec3 fetchEyePos(vec2 uv)
        {
            float depth = texture(u_Depth, clamp(uv, vec2(0.0), vec2(1.0))).r;
            if (depth >= 0.999999)
                return vec3(0.0, 0.0, 1.0e8);
            return reconstructViewPosition(uv, depth);
        }
        float length2(vec3 v) { return dot(v, v); }
        vec3 minDiff(vec3 P, vec3 Pr, vec3 Pl)
        {
            vec3 v1 = Pr - P;
            vec3 v2 = P - Pl;
            return (length2(v1) < length2(v2)) ? v1 : v2;
        }
        float falloff(float d2) { return d2 * u_NegInvR2 + 1.0; }
        vec2 snapUVOffset(vec2 uv) { return round(uv * u_FullResolution) * u_InvFullResolution; }
        float tanToSin(float x) { return x * inversesqrt(x * x + 1.0); }
        float tangentFromVector(vec3 t) { return -t.z * invLength(t.xy); }
        float biasedTangent(vec3 t) { return tangentFromVector(t) + u_TanAngleBias; }

        float integrateOcclusion(vec2 uv0, vec2 snappedDuv, vec3 P, vec3 dPdu, vec3 dPdv, inout float tanH)
        {
            vec3 T1 = snappedDuv.x * dPdu + snappedDuv.y * dPdv;
            float tanT = biasedTangent(T1);
            float sinT = tanToSin(tanT);
            vec3 S = fetchEyePos(uv0 + snappedDuv);
            float tanS = tangent(P, S);
            float sinS = tanToSin(tanS);
            float d2 = length2(S - P);
            if ((d2 < u_Radius * u_Radius) && (tanS > tanT))
            {
                tanH = max(tanH, tanS);
                return falloff(d2) * (sinS - sinT);
            }
            return 0.0;
        }

        float horizonOcclusion(vec2 deltaUV, vec2 texelDeltaUV, vec2 uv0, vec3 P, float numSteps, float randStep, vec3 dPdu, vec3 dPdv)
        {
            float ao = 0.0;
            vec2 uv = uv0 + snapUVOffset(randStep * deltaUV);
            deltaUV = snapUVOffset(deltaUV);
            vec3 T = deltaUV.x * dPdu + deltaUV.y * dPdv;
            float tanH = biasedTangent(T);
            vec2 snappedDuv = snapUVOffset(randStep * deltaUV + texelDeltaUV);
            ao = integrateOcclusion(uv0, snappedDuv, P, dPdu, dPdv, tanH);
            float sinH = tanH / sqrt(1.0 + tanH * tanH);
            for (int j = 1; j <= NUM_STEPS; ++j)
            {
                if (float(j) > numSteps - 1.0) break;
                uv += deltaUV;
                vec3 S = fetchEyePos(uv);
                float tanS = tangent(P, S);
                float d2 = length2(S - P);
                if ((d2 < u_Radius * u_Radius) && (tanS > tanH))
                {
                    float sinS = tanS / sqrt(1.0 + tanS * tanS);
                    ao += falloff(d2) * (sinS - sinH);
                    tanH = tanS;
                    sinH = sinS;
                }
            }
            return ao;
        }

        vec2 rotateDirections(vec2 dir, vec2 cs)
        {
            return vec2(dir.x * cs.x - dir.y * cs.y, dir.x * cs.y + dir.y * cs.x);
        }

        void computeSteps(inout vec2 stepSizeUv, inout float numSteps, float rayRadiusPix, float rand)
        {
            numSteps = min(float(NUM_STEPS), rayRadiusPix);
            float stepSizePix = max(float(u_StepSize), rayRadiusPix / (numSteps + 1.0));
            float maxNumSteps = u_MaxRadiusPixels / stepSizePix;
            if (maxNumSteps < numSteps)
            {
                numSteps = floor(maxNumSteps + rand);
                numSteps = max(numSteps, 1.0);
                stepSizePix = u_MaxRadiusPixels / numSteps;
            }
            stepSizeUv = stepSizePix * u_InvFullResolution;
        }

        void main()
        {
            float rawDepth = texture(u_Depth, v_UV).r;
            if (rawDepth >= 0.999999)
            {
                o_AODepth = vec2(1.0, 1.0e8);
                return;
            }
            vec3 P = reconstructViewPosition(v_UV, rawDepth);
            vec3 rand = texture(u_Random, gl_FragCoord.xy / 4.0).xyz;
            vec2 rayRadiusUv = 0.5 * u_Radius * u_FocalLen / max(P.z, 0.0001);
            float rayRadiusPix = rayRadiusUv.x * u_FullResolution.x;
            if (rayRadiusPix < 1.0)
            {
                o_AODepth = vec2(1.0, P.z);
                return;
            }
            float numSteps;
            vec2 stepSize;
            computeSteps(stepSize, numSteps, rayRadiusPix, rand.z);
            vec3 Pr = fetchEyePos(v_UV + vec2(u_InvFullResolution.x, 0.0));
            vec3 Pl = fetchEyePos(v_UV - vec2(u_InvFullResolution.x, 0.0));
            vec3 Pt = fetchEyePos(v_UV + vec2(0.0, u_InvFullResolution.y));
            vec3 Pb = fetchEyePos(v_UV - vec2(0.0, u_InvFullResolution.y));
            vec3 dPdu = minDiff(P, Pr, Pl);
            vec3 dPdv = minDiff(P, Pt, Pb) * (u_FullResolution.y * u_InvFullResolution.x);
            float ao = 0.0;
            float alpha = 2.0 * PI / float(NUM_DIRECTIONS);
            for (int d = 0; d < NUM_DIRECTIONS; ++d)
            {
                float angle = alpha * float(d);
                vec2 dir = rotateDirections(vec2(cos(angle), sin(angle)), rand.xy);
                ao += horizonOcclusion(dir * stepSize, dir * u_InvFullResolution, v_UV, P, numSteps, rand.z, dPdu, dPdv);
            }
            ao = clamp(1.0 - ao / float(NUM_DIRECTIONS) * u_Strength, 0.0, 1.0);
            o_AODepth = vec2(ao, P.z);
        }
    )";
    const std::string hbaoBlurFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec2 o_AODepth;
        uniform sampler2D u_AODepth;
        uniform vec2 u_InvResolution;
        uniform vec2 u_Direction;
        uniform int u_BlurRadius;
        uniform float u_BlurSharpness;

        float bilateralWeight(float radius, float sampleDepth, float centerDepth)
        {
            float blurSigma = max(float(u_BlurRadius) * 0.5, 1.0);
            float blurFalloff = 1.0 / (2.0 * blurSigma * blurSigma);
            float dz = sampleDepth - centerDepth;
            return exp2(-radius * radius * blurFalloff - dz * dz * u_BlurSharpness);
        }

        void main()
        {
            vec2 center = texture(u_AODepth, v_UV).rg;
            float aoTotal = center.x;
            float weightTotal = 1.0;
            for (int i = 1; i <= 16; ++i)
            {
                if (i > u_BlurRadius) break;
                vec2 offset = u_Direction * u_InvResolution * float(i);
                vec2 a = texture(u_AODepth, v_UV + offset).rg;
                vec2 b = texture(u_AODepth, v_UV - offset).rg;
                float wa = bilateralWeight(float(i), a.y, center.y);
                float wb = bilateralWeight(float(i), b.y, center.y);
                aoTotal += a.x * wa + b.x * wb;
                weightTotal += wa + wb;
            }
            o_AODepth = vec2(aoTotal / max(weightTotal, 0.0001), center.y);
        }
    )";
    const std::string hbaoCompositeFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec4 o_Color;
        uniform sampler2D u_SceneColor;
        uniform sampler2D u_AODepth;
        uniform int u_DebugMode;
        uniform float u_PowerExponent;
        void main()
        {
            vec4 scene = texture(u_SceneColor, v_UV);
            float ao = pow(clamp(texture(u_AODepth, v_UV).r, 0.0, 1.0), max(u_PowerExponent, 0.01));
            if (u_DebugMode == 1)
            {
                o_Color = vec4(vec3(ao), 1.0);
                return;
            }
            o_Color = vec4(scene.rgb * ao, scene.a);
        }
    )";
    m_AOShader = Shader::Create("SSAO", fullscreenVertex, aoFragment);
    m_BlurShader = Shader::Create("SSAOBlur", fullscreenVertex, blurFragment);
    m_CompositeShader = Shader::Create("SSAOComposite", fullscreenVertex, compositeFragment);
    m_HBAOShader = Shader::Create("SSAO11HBAO", fullscreenVertex, hbaoFragment);
    m_HBAOBlurShader = Shader::Create("SSAO11HBAOBlur", fullscreenVertex, hbaoBlurFragment);
    m_HBAOCompositeShader = Shader::Create("SSAO11HBAOComposite", fullscreenVertex, hbaoCompositeFragment);
    glCreateVertexArrays(1, &m_QuadVAO);

    std::default_random_engine generator(0);
    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
    m_SampleKernel.reserve(64);
    for (int i = 0; i < 64; ++i)
    {
        glm::vec3 sample(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator));
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = (float)i / 64.0f;
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        m_SampleKernel.push_back(sample * scale);
    }

    std::vector<glm::vec3> noise;
    noise.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        noise.emplace_back(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            0.0f);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &m_NoiseTexture);
    glTextureStorage2D(m_NoiseTexture, 1, GL_RGB16F, 4, 4);
    glTextureSubImage2D(m_NoiseTexture, 0, 0, 0, 4, 4, GL_RGB, GL_FLOAT, noise.data());
    glTextureParameteri(m_NoiseTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_NoiseTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_NoiseTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_NoiseTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);

    std::vector<glm::vec4> hbaoRandom;
    hbaoRandom.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        const float angle = randomFloats(generator) * 6.28318530718f;
        hbaoRandom.emplace_back(cosf(angle), sinf(angle), randomFloats(generator), 1.0f);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &m_HBAORandomTexture);
    glTextureStorage2D(m_HBAORandomTexture, 1, GL_RGBA32F, 4, 4);
    glTextureSubImage2D(m_HBAORandomTexture, 0, 0, 0, 4, 4, GL_RGBA, GL_FLOAT, hbaoRandom.data());
    glTextureParameteri(m_HBAORandomTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_HBAORandomTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_HBAORandomTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_HBAORandomTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
    Invalidate();
#endif
#ifdef G_DX11
    CreateDX11Resources();
#endif
}

SSAO::~SSAO()
{
#ifdef G_OPENGL
    glDeleteVertexArrays(1, &m_QuadVAO);
    glDeleteTextures(1, &m_NoiseTexture);
    glDeleteTextures(1, &m_HBAORandomTexture);
    glDeleteFramebuffers(1, &m_AOFBO);
    glDeleteFramebuffers(1, &m_BlurFBO);
    glDeleteFramebuffers(1, &m_OutputFBO);
    glDeleteFramebuffers(1, &m_HBAOFBO);
    glDeleteFramebuffers(1, &m_HBAOBlurFBO);
    glDeleteTextures(1, &m_AOTexture);
    glDeleteTextures(1, &m_BlurTexture);
    glDeleteTextures(1, &m_OutputTexture);
    glDeleteTextures(1, &m_HBAOTexture);
    glDeleteTextures(1, &m_HBAOBlurTexture);
#endif
#ifdef G_DX11
    DestroyDX11Resources();
#endif
}

void SSAO::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || (width == m_Width && height == m_Height))
        return;
    m_Width = width;
    m_Height = height;
    Invalidate();
}

void SSAO::Invalidate()
{
#ifdef G_OPENGL
    m_AOWidth = glm::max(1u, (uint32_t)glm::ceil((float)m_Width * m_AOScale));
    m_AOHeight = glm::max(1u, (uint32_t)glm::ceil((float)m_Height * m_AOScale));

    if (m_AOFBO)
    {
        glDeleteFramebuffers(1, &m_AOFBO);
        glDeleteFramebuffers(1, &m_BlurFBO);
        glDeleteFramebuffers(1, &m_OutputFBO);
        glDeleteFramebuffers(1, &m_HBAOFBO);
        glDeleteFramebuffers(1, &m_HBAOBlurFBO);
        glDeleteTextures(1, &m_AOTexture);
        glDeleteTextures(1, &m_BlurTexture);
        glDeleteTextures(1, &m_OutputTexture);
        glDeleteTextures(1, &m_HBAOTexture);
        glDeleteTextures(1, &m_HBAOBlurTexture);
    }

    glCreateFramebuffers(1, &m_AOFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_AOTexture);
    glTextureStorage2D(m_AOTexture, 1, GL_R16F, m_AOWidth, m_AOHeight);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_AOFBO, GL_COLOR_ATTACHMENT0, m_AOTexture, 0);
    if (glCheckNamedFramebufferStatus(m_AOFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO framebuffer is incomplete!");

    glCreateFramebuffers(1, &m_BlurFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_BlurTexture);
    glTextureStorage2D(m_BlurTexture, 1, GL_R16F, m_AOWidth, m_AOHeight);
    glTextureParameteri(m_BlurTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_BlurTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_BlurTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_BlurTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_BlurFBO, GL_COLOR_ATTACHMENT0, m_BlurTexture, 0);
    if (glCheckNamedFramebufferStatus(m_BlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO blur framebuffer is incomplete!");

    glCreateFramebuffers(1, &m_OutputFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_OutputTexture);
    glTextureStorage2D(m_OutputTexture, 1, GL_RGBA16F, m_Width, m_Height);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_OutputTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_OutputFBO, GL_COLOR_ATTACHMENT0, m_OutputTexture, 0);
    if (glCheckNamedFramebufferStatus(m_OutputFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO composite framebuffer is incomplete!");

    glCreateFramebuffers(1, &m_HBAOFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_HBAOTexture);
    glTextureStorage2D(m_HBAOTexture, 1, GL_RG16F, m_AOWidth, m_AOHeight);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_HBAOFBO, GL_COLOR_ATTACHMENT0, m_HBAOTexture, 0);
    if (glCheckNamedFramebufferStatus(m_HBAOFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO11 HBAO framebuffer is incomplete!");

    glCreateFramebuffers(1, &m_HBAOBlurFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_HBAOBlurTexture);
    glTextureStorage2D(m_HBAOBlurTexture, 1, GL_RG16F, m_AOWidth, m_AOHeight);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_HBAOBlurFBO, GL_COLOR_ATTACHMENT0, m_HBAOBlurTexture, 0);
    if (glCheckNamedFramebufferStatus(m_HBAOBlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO11 HBAO blur framebuffer is incomplete!");
#endif
#ifdef G_DX11
    DestroyDX11Resources();
    CreateDX11Resources();
#endif
}

void SSAO::Render(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
    const glm::mat4& view, const glm::mat4& projection)
{
    if (!m_Enabled)
        return;
#ifdef G_OPENGL
    if (m_Algorithm == Algorithm::SSAO11HBAO)
        RenderHBAO(colorTexture, depthTexture, projection);
    else
        RenderKernelSSAO(colorTexture, depthTexture, normalTexture, view, projection);
#endif
#ifdef G_DX11
    if (!m_DX11Ready || !colorTexture || !depthTexture || !normalTexture)
    {
        if (Renderer::GetAPI() == Renderer::API::DX11 && colorTexture && depthTexture && normalTexture)
            CreateDX11Resources();
        if (!m_DX11Ready)
            return;
    }
    if (m_Algorithm == Algorithm::SSAO11HBAO && (!m_DX11HBAOPS || !m_DX11HBAOBlurPS || !m_DX11HBAOCompositePS))
        CreateDX11Resources();
    else if (m_Algorithm == Algorithm::KernelSSAO && (!m_DX11AOPS || !m_DX11BlurPS || !m_DX11CompositePS))
        CreateDX11Resources();
    if (!m_DX11Ready)
        return;
    if (m_Algorithm == Algorithm::SSAO11HBAO)
        RenderDX11HBAO(colorTexture, depthTexture, projection);
    else
        RenderDX11KernelSSAO(colorTexture, depthTexture, normalTexture, view, projection);
#endif
}

uint64_t SSAO::GetAOTexture() const
{
#ifdef G_DX11
    if (Renderer::GetAPI() == Renderer::API::DX11)
        return (uint64_t)m_DX11BlurSRV.Get();
#endif
    return m_Algorithm == Algorithm::SSAO11HBAO ? m_HBAOTexture : m_BlurTexture;
}

uint64_t SSAO::GetOutputTexture() const
{
#ifdef G_DX11
    if (Renderer::GetAPI() == Renderer::API::DX11)
        return (uint64_t)m_DX11OutputSRV.Get();
#endif
    return m_OutputTexture;
}

void SSAO::RenderKernelSSAO(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
    const glm::mat4& view, const glm::mat4& projection)
{
#ifdef G_OPENGL
    if (!m_AOShader || !m_BlurShader || !m_CompositeShader)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_QuadVAO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_AOFBO);
    glViewport(0, 0, m_AOWidth, m_AOHeight);
    m_AOShader->Bind();
    m_AOShader->SetInt("u_Depth", 0);
    m_AOShader->SetInt("u_Normal", 1);
    m_AOShader->SetInt("u_Noise", 2);
    m_AOShader->SetMat4("u_View", view);
    m_AOShader->SetMat4("u_Projection", projection);
    m_AOShader->SetMat4("u_InverseProjection", glm::inverse(projection));
    m_AOShader->SetFloat2("u_Resolution", glm::vec2(m_AOWidth, m_AOHeight));
    m_AOShader->SetFloat("u_Radius", m_Radius);
    m_AOShader->SetFloat("u_Bias", m_Bias);
    m_AOShader->SetFloat("u_Strength", m_Strength);
    m_AOShader->SetVec3Array("u_Samples", &m_SampleKernel[0].x, (uint32_t)m_SampleKernel.size());
    glBindTextureUnit(0, (uint32_t)depthTexture);
    glBindTextureUnit(1, (uint32_t)normalTexture);
    glBindTextureUnit(2, m_NoiseTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, m_BlurFBO);
    glViewport(0, 0, m_AOWidth, m_AOHeight);
    m_BlurShader->Bind();
    m_BlurShader->SetInt("u_AO", 0);
    m_BlurShader->SetInt("u_Depth", 1);
    m_BlurShader->SetMat4("u_InverseProjection", glm::inverse(projection));
    m_BlurShader->SetFloat2("u_Resolution", glm::vec2(m_AOWidth, m_AOHeight));
    glBindTextureUnit(0, m_AOTexture);
    glBindTextureUnit(1, (uint32_t)depthTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
    glViewport(0, 0, m_Width, m_Height);
    m_CompositeShader->Bind();
    m_CompositeShader->SetInt("u_SceneColor", 0);
    m_CompositeShader->SetInt("u_AO", 1);
    m_CompositeShader->SetInt("u_DebugMode", m_DebugMode);
    glBindTextureUnit(0, (uint32_t)colorTexture);
    glBindTextureUnit(1, m_BlurTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#else
    (void)colorTexture;
    (void)depthTexture;
    (void)normalTexture;
    (void)view;
    (void)projection;
#endif
}

void SSAO::RenderHBAO(uint64_t colorTexture, uint64_t depthTexture, const glm::mat4& projection)
{
#ifdef G_OPENGL
    if (!m_HBAOShader || !m_HBAOBlurShader || !m_HBAOCompositeShader)
        return;

    const glm::mat4 inverseProjection = glm::inverse(projection);
    const float focalX = projection[0][0];
    const float focalY = projection[1][1];
    const float clampedRadius = glm::max(m_Radius, 0.0001f);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_QuadVAO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_HBAOFBO);
    glViewport(0, 0, m_AOWidth, m_AOHeight);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_HBAOShader->Bind();
    m_HBAOShader->SetInt("u_Depth", 0);
    m_HBAOShader->SetInt("u_Random", 1);
    m_HBAOShader->SetMat4("u_InverseProjection", inverseProjection);
    m_HBAOShader->SetFloat2("u_FullResolution", glm::vec2(m_AOWidth, m_AOHeight));
    m_HBAOShader->SetFloat2("u_InvFullResolution", glm::vec2(1.0f / (float)m_AOWidth, 1.0f / (float)m_AOHeight));
    m_HBAOShader->SetFloat2("u_FocalLen", glm::vec2(focalX, focalY));
    m_HBAOShader->SetFloat("u_Radius", clampedRadius);
    m_HBAOShader->SetFloat("u_NegInvR2", -1.0f / (clampedRadius * clampedRadius));
    m_HBAOShader->SetFloat("u_MaxRadiusPixels", m_MaxRadiusPixels);
    m_HBAOShader->SetFloat("u_TanAngleBias", tanf(glm::radians(m_AngleBiasDegrees)));
    m_HBAOShader->SetFloat("u_Strength", m_Strength);
    m_HBAOShader->SetInt("u_StepSize", m_StepSize);
    glBindTextureUnit(0, (uint32_t)depthTexture);
    glBindTextureUnit(1, m_HBAORandomTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    RenderHBAOBlur(m_HBAOBlurFBO, m_HBAOTexture, 1.0f, 0.0f);
    RenderHBAOBlur(m_HBAOFBO, m_HBAOBlurTexture, 0.0f, 1.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
    glViewport(0, 0, m_Width, m_Height);
    m_HBAOCompositeShader->Bind();
    m_HBAOCompositeShader->SetInt("u_SceneColor", 0);
    m_HBAOCompositeShader->SetInt("u_AODepth", 1);
    m_HBAOCompositeShader->SetInt("u_DebugMode", m_DebugMode);
    m_HBAOCompositeShader->SetFloat("u_PowerExponent", m_PowerExponent);
    glBindTextureUnit(0, (uint32_t)colorTexture);
    glBindTextureUnit(1, m_HBAOTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#else
    (void)colorTexture;
    (void)depthTexture;
    (void)projection;
#endif
}

void SSAO::RenderHBAOBlur(uint32_t fbo, uint32_t inputTexture, float dx, float dy)
{
#ifdef G_OPENGL
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_AOWidth, m_AOHeight);
    m_HBAOBlurShader->Bind();
    m_HBAOBlurShader->SetInt("u_AODepth", 0);
    m_HBAOBlurShader->SetFloat2("u_InvResolution", glm::vec2(1.0f / (float)m_AOWidth, 1.0f / (float)m_AOHeight));
    m_HBAOBlurShader->SetFloat2("u_Direction", glm::vec2(dx, dy));
    m_HBAOBlurShader->SetInt("u_BlurRadius", m_BlurRadius);
    m_HBAOBlurShader->SetFloat("u_BlurSharpness", m_BlurSharpness);
    glBindTextureUnit(0, inputTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);
#else
    (void)fbo;
    (void)inputTexture;
    (void)dx;
    (void)dy;
#endif
}

#ifdef G_DX11

static const char* s_DX11FullscreenVS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
VSOut VSMain(uint vertexID : SV_VertexID)
{
    VSOut o;
    float2 p = float2((vertexID << 1) & 2, vertexID & 2);
    o.UV = float2(p.x, p.y);
    o.Position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    return o;
}
)";

// KernelSSAO shaders
static const char* s_DX11AOPS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
cbuffer AOCB : register(b0)
{
    float4x4 u_View;
    float4x4 u_Projection;
    float4x4 u_InverseProjection;
    float4 u_Resolution;
    float4 u_Params;
    float4 u_Samples[64];
};
Texture2D<float> u_Depth : register(t0);
Texture2D<float4> u_Normal : register(t1);
Texture2D<float4> u_Noise : register(t2);
SamplerState u_sampler : register(s0);

float3 ReconstructViewPos(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 view = mul(u_InverseProjection, clip);
    return view.xyz / view.w;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float centerDepth = u_Depth.Sample(u_sampler, input.UV);
    if (centerDepth >= 0.999999) return 1.0;
    float3 center = ReconstructViewPos(input.UV, centerDepth);
    float3 normal = normalize(mul((float3x3)u_View, u_Normal.Sample(u_sampler, input.UV).xyz));
    float3 randomVec = normalize(u_Noise.Sample(u_sampler, input.UV * u_Resolution.xy / 4.0).xyz);
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    float occlusion = 0.0;
    [unroll] for (int i = 0; i < 64; ++i)
    {
        float3 samplePos = center + mul(u_Samples[i].xyz, tbn) * u_Params.x;
        float4 offset = mul(u_Projection, float4(samplePos, 1.0));
        offset.xyz /= offset.w;
        offset.xy = float2(offset.x * 0.5 + 0.5, 0.5 - offset.y * 0.5);
        float sampleDepth = u_Depth.Sample(u_sampler, clamp(offset.xy, 0.0, 1.0));
        if (sampleDepth >= 0.999999) continue;
        float sampleZ = ReconstructViewPos(offset.xy, sampleDepth).z;
        float rangeCheck = smoothstep(0.0, 1.0, u_Params.x / max(abs(center.z - sampleZ), 0.0001));
        occlusion += (sampleZ >= samplePos.z + u_Params.y ? 1.0 : 0.0) * rangeCheck;
    }
    return pow(saturate(1.0 - occlusion / 64.0), max(u_Params.z, 0.001));
}
)";

static const char* s_DX11BlurPS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
cbuffer BlurCB : register(b0)
{
    float4x4 u_InverseProjection;
    float4 u_Resolution;
};
Texture2D<float> u_AO : register(t0);
Texture2D<float> u_Depth : register(t1);
SamplerState u_sampler : register(s0);

float3 ReconstructViewPos(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 view = mul(u_InverseProjection, clip);
    return view.xyz / view.w;
}

float PSMain(VSOut input) : SV_TARGET
{
    float centerDepth = u_Depth.Sample(u_sampler, input.UV);
    if (centerDepth >= 0.999999) return 1.0;
    float3 centerPos = ReconstructViewPos(input.UV, centerDepth);
    float2 texel = 1.0 / u_Resolution.xy;
    float sum = 0.0, weightSum = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y)
        [unroll] for (int x = -2; x <= 2; ++x)
        {
            float2 uv = clamp(input.UV + float2(x, y) * texel, 0.0, 1.0);
            float sd = u_Depth.Sample(u_sampler, uv);
            if (sd >= 0.999999) continue;
            float sp = ReconstructViewPos(uv, sd).z;
            float w = exp(-dot(float2(x, y), float2(x, y)) / 4.0) * exp(-abs(sp - centerPos.z) * 0.4);
            sum += u_AO.Sample(u_sampler, uv) * w;
            weightSum += w;
        }
    return sum / max(weightSum, 0.0001);
}
)";

static const char* s_DX11CompositePS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
cbuffer CompositeCB : register(b0) { int u_DebugMode; };
Texture2D<float4> u_SceneColor : register(t0);
Texture2D<float> u_AO : register(t1);
SamplerState u_sampler : register(s0);

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 scene = u_SceneColor.Sample(u_sampler, input.UV);
    float ao = u_AO.Sample(u_sampler, input.UV);
    if (u_DebugMode != 0) return float4(ao.xxx, 1.0);
    return float4(scene.rgb * lerp(0.45, 1.0, ao), scene.a);
}
)";

// HBAO shaders
static const char* s_DX11HBAOPS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
cbuffer HBAOCB : register(b0)
{
    float4x4 u_InverseProjection;
    float4 u_FullResolution;
    float4 u_InvFullResolution;
    float4 u_FocalLen;
    float4 u_Params;  // x=radius, y=negInvR2, z=maxRadiusPixels, w=tanAngleBias
    float4 u_Strength;
};
Texture2D<float> u_Depth : register(t0);
Texture2D<float4> u_Random : register(t1);
SamplerState u_sampler : register(s0);

float3 ReconstructViewPos(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 view = mul(u_InverseProjection, clip);
    view.xyz /= view.w;
    return float3(view.xy, -view.z);
}
float length2(float3 v) { return dot(v, v); }
float2 snapUVOffset(float2 uv) { return round(uv * u_FullResolution.xy) * u_InvFullResolution.xy; }
float invLength(float2 v) { return rsqrt(max(dot(v, v), 0.000001)); }
float tangent(float3 P, float3 S) { return (P.z - S.z) * invLength(S.xy - P.xy); }
float toSin(float x) { return x * rsqrt(x * x + 1.0); }
float falloff(float d2) { return d2 * u_Params.y + 1.0; }

float2 PSMain(VSOut input) : SV_TARGET
{
    float rawDepth = u_Depth.Sample(u_sampler, input.UV);
    if (rawDepth >= 0.999999) return float2(1.0, 1.0e8);
    float3 P = ReconstructViewPos(input.UV, rawDepth);
    float3 rand = u_Random.Sample(u_sampler, floor(input.UV * u_FullResolution.xy) / 4.0).xyz;
    float2 rayRadiusUv = 0.5 * u_Params.x * u_FocalLen.xy / max(P.z, 0.0001);
    float rayRadiusPix = rayRadiusUv.x * u_FullResolution.x;
    if (rayRadiusPix < 1.0) return float2(1.0, P.z);

    float numSteps = min(6.0, rayRadiusPix);
    float stepSizePix = max(4.0, rayRadiusPix / (numSteps + 1.0));
    float maxNumSteps = u_Params.z / stepSizePix;
    if (maxNumSteps < numSteps) { numSteps = floor(maxNumSteps + rand.z); numSteps = max(numSteps, 1.0); stepSizePix = u_Params.z / numSteps; }
    float2 stepSize = stepSizePix * u_InvFullResolution.xy;

    float2 duv = float2(u_InvFullResolution.x, 0.0);
    float3 Pr = ReconstructViewPos(input.UV + duv, u_Depth.Sample(u_sampler, input.UV + duv));
    float3 Pl = ReconstructViewPos(input.UV - duv, u_Depth.Sample(u_sampler, input.UV - duv));
    float2 duvy = float2(0.0, u_InvFullResolution.y);
    float3 Pt = ReconstructViewPos(input.UV + duvy, u_Depth.Sample(u_sampler, input.UV + duvy));
    float3 Pb = ReconstructViewPos(input.UV - duvy, u_Depth.Sample(u_sampler, input.UV - duvy));
    float3 dPdu = length2(Pr - P) < length2(P - Pl) ? Pr - P : P - Pl;
    float3 dPdv = (length2(Pt - P) < length2(P - Pb) ? Pt - P : P - Pb) * (u_FullResolution.y * u_InvFullResolution.x);

    float ao = 0.0;
    float alpha = 6.2831853 / 8.0;
    [unroll] for (int d = 0; d < 8; ++d)
    {
        float angle = alpha * d;
        float ca = cos(angle), sa = sin(angle);
        float2 dir = float2(ca * rand.x - sa * rand.y, ca * rand.y + sa * rand.x);
        float2 duvStep = snapUVOffset(dir * stepSize);
        float2 uv = input.UV + snapUVOffset(rand.z * duvStep);
        float3 T = duvStep.x * dPdu + duvStep.y * dPdv;
        float tanH = -T.z * rsqrt(max(dot(T.xy, T.xy), 0.000001)) + u_Params.w;
        float sinH = toSin(tanH);
        [loop] for (int j = 0; j < 6; ++j)
        {
            uv += duvStep;
            float3 S = ReconstructViewPos(uv, u_Depth.Sample(u_sampler, uv));
            float tanS = tangent(P, S);
            float d2 = dot(S - P, S - P);
            if (d2 < u_Params.x * u_Params.x && tanS > tanH)
            {
                float sinS = toSin(tanS);
                ao += falloff(d2) * (sinS - sinH);
                tanH = tanS;
                sinH = sinS;
            }
        }
    }
    ao = saturate(1.0 - ao / 8.0 * u_Strength.x);
    return float2(ao, max(P.z, 0.0001));
}
)";

static const char* s_DX11HBAOBlurPS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
cbuffer HBAOBlurCB : register(b0)
{
    float4 u_InvResolution;
    float4 u_Direction;
    int u_BlurRadius;
    float u_BlurSharpness;
};
Texture2D<float2> u_AODepth : register(t0);
SamplerState u_sampler : register(s0);

float2 PSMain(VSOut input) : SV_TARGET
{
    float2 center = u_AODepth.Sample(u_sampler, input.UV);
    float aoTotal = center.x;
    float weightTotal = 1.0;
    float sigma = max(u_BlurRadius * 0.5, 1.0);
    float blurFalloff = 1.0 / (2.0 * sigma * sigma);
    [unroll] for (int i = 1; i <= 16; ++i)
    {
        if (i > u_BlurRadius) break;
        float2 off = u_Direction.xy * u_InvResolution.xy * i;
        float2 a = u_AODepth.Sample(u_sampler, input.UV + off);
        float2 b = u_AODepth.Sample(u_sampler, input.UV - off);
        float wa = exp2(-i * i * blurFalloff - (a.y - center.y) * (a.y - center.y) * u_BlurSharpness);
        float wb = exp2(-i * i * blurFalloff - (b.y - center.y) * (b.y - center.y) * u_BlurSharpness);
        aoTotal += a.x * wa + b.x * wb;
        weightTotal += wa + wb;
    }
    return float2(aoTotal / max(weightTotal, 0.0001), center.y);
}
)";

static const char* s_DX11HBAOCompositePS = R"(
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
cbuffer HBAOCompositeCB : register(b0)
{
    int u_DebugMode;
    float u_PowerExponent;
};
Texture2D<float4> u_SceneColor : register(t0);
Texture2D<float2> u_AODepth : register(t1);
SamplerState u_sampler : register(s0);

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 scene = u_SceneColor.Sample(u_sampler, input.UV);
    float ao = pow(saturate(u_AODepth.Sample(u_sampler, input.UV).x), max(u_PowerExponent, 0.01));
    if (u_DebugMode != 0) return float4(ao.xxx, 1.0);
    return float4(scene.rgb * ao, scene.a);
}
)";

static HRESULT SafeD3DCompile(const char* source, SIZE_T srcSize, const char* entry, const char* target, UINT flags, ID3DBlob** outBlob, ID3DBlob** outErrors)
{
    __try {
        return D3DCompile(source, srcSize, nullptr, nullptr, nullptr, entry, target, flags, 0, outBlob, outErrors);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

static Microsoft::WRL::ComPtr<ID3DBlob> CompileHLSL(const char* source, const char* entry, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    ID3DBlob* pBlob = nullptr;
    ID3DBlob* pErrors = nullptr;
    HRESULT hr = SafeD3DCompile(source, strlen(source), entry, target, flags, &pBlob, &pErrors);
    if (FAILED(hr))
    {
        if (pErrors)
        {
            ERROR("DX11 SSAO shader compile error ({}): {}", entry, (const char*)pErrors->GetBufferPointer());
            pErrors->Release();
        }
        else
        {
            ERROR("DX11 SSAO shader compilation failed for {}", entry);
        }
        return nullptr;
    }
    if (pErrors) pErrors->Release();
    return Microsoft::WRL::ComPtr<ID3DBlob>(pBlob);
}

void SSAO::CreateDX11Resources()
{
    m_DX11Ready = false;
    if (m_Width == 0 || m_Height == 0)
        return;

    auto device = DX11Context::GetDevice();
    if (!device)
    {
        ERROR("SSAO DX11: null device");
        return;
    }

    m_DX11AOWidth = glm::max(1u, (uint32_t)glm::ceil((float)m_Width * m_DX11AOScale));
    m_DX11AOHeight = glm::max(1u, (uint32_t)glm::ceil((float)m_Height * m_DX11AOScale));

    // Create textures
    auto makeTexture = [&](uint32_t w, uint32_t h, DXGI_FORMAT fmt, UINT bindFlags,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& tex,
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& rtv,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC desc = { w, h, 1, 1, fmt, {1,0}, D3D11_USAGE_DEFAULT, bindFlags };
        device->CreateTexture2D(&desc, nullptr, &tex);
        if (bindFlags & D3D11_BIND_RENDER_TARGET)
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = { fmt, D3D11_RTV_DIMENSION_TEXTURE2D };
            device->CreateRenderTargetView(tex.Get(), &rtvDesc, &rtv);
        }
        if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { fmt, D3D11_SRV_DIMENSION_TEXTURE2D };
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(tex.Get(), &srvDesc, &srv);
        }
    };

    makeTexture(m_DX11AOWidth, m_DX11AOHeight, DXGI_FORMAT_R16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        m_DX11AOTexture, m_DX11AORTV, m_DX11AOSRV);
    makeTexture(m_DX11AOWidth, m_DX11AOHeight, DXGI_FORMAT_R16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        m_DX11BlurTexture, m_DX11BlurRTV, m_DX11BlurSRV);
    makeTexture(m_Width, m_Height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        m_DX11OutputTexture, m_DX11OutputRTV, m_DX11OutputSRV);
    makeTexture(m_DX11AOWidth, m_DX11AOHeight, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        m_DX11HBAOTexture, m_DX11HBAORTV, m_DX11HBAOSRV);
    makeTexture(m_DX11AOWidth, m_DX11AOHeight, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        m_DX11HBAOBlurTexture, m_DX11HBAOBlurRTV, m_DX11HBAOBlurSRV);

    // Generate noise
    std::default_random_engine gen(0);
    std::uniform_real_distribution<float> rnd(0.0f, 1.0f);

    { // Keep CPU texel layout identical to the DXGI format.
        std::vector<glm::vec4> noise(16);
        for (int i = 0; i < 16; i++)
            noise[i] = glm::vec4(rnd(gen) * 2.0f - 1.0f, rnd(gen) * 2.0f - 1.0f, 0.0f, 1.0f);

        D3D11_TEXTURE2D_DESC desc = { 4, 4, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE };
        D3D11_SUBRESOURCE_DATA data = { noise.data(), 4 * sizeof(glm::vec4), 0 };
        device->CreateTexture2D(&desc, &data, &m_DX11NoiseTexture);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D };
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_DX11NoiseTexture.Get(), &srvDesc, &m_DX11NoiseSRV);
    }

    { // HBAO Random (4x4 RGBA32F)
        std::vector<glm::vec4> random(16);
        for (int i = 0; i < 16; i++)
        {
            float angle = rnd(gen) * 6.2831853f;
            random[i] = glm::vec4(cosf(angle), sinf(angle), rnd(gen), 1.0f);
        }
        D3D11_TEXTURE2D_DESC desc = { 4, 4, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE };
        D3D11_SUBRESOURCE_DATA data = { random.data(), 4 * 16, 0 };
        device->CreateTexture2D(&desc, &data, &m_DX11HBAORandomTexture);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D };
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_DX11HBAORandomTexture.Get(), &srvDesc, &m_DX11HBAORandomSRV);
    }

    // Compile shaders once. Resizing only needs texture/RTV/SRV recreation.
    if (!m_DX11VS)
    {
        auto vsBlob = CompileHLSL(s_DX11FullscreenVS, "VSMain", "vs_5_0");
        if (!vsBlob)
        {
            ERROR("SSAO DX11: failed to compile fullscreen vertex shader");
            return;
        }
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_DX11VS);
    }

    auto compilePS = [&](const char* src, Microsoft::WRL::ComPtr<ID3D11PixelShader>& ps) {
        if (ps)
            return;
        auto blob = CompileHLSL(src, "PSMain", "ps_5_0");
        if (blob)
            device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
        else
            ERROR("SSAO DX11: failed to compile pixel shader");
    };
    if (m_Algorithm == Algorithm::SSAO11HBAO)
    {
        compilePS(s_DX11HBAOPS, m_DX11HBAOPS);
        compilePS(s_DX11HBAOBlurPS, m_DX11HBAOBlurPS);
        compilePS(s_DX11HBAOCompositePS, m_DX11HBAOCompositePS);
    }
    else
    {
        compilePS(s_DX11AOPS, m_DX11AOPS);
        compilePS(s_DX11BlurPS, m_DX11BlurPS);
        compilePS(s_DX11CompositePS, m_DX11CompositePS);
    }

    // The fullscreen pass is vertexless and must use a null input layout.
    m_DX11InputLayout.Reset();

    // Create constant buffers
    auto makeCB = [&](UINT size, Microsoft::WRL::ComPtr<ID3D11Buffer>& cb) {
        D3D11_BUFFER_DESC desc = { (size + 15) & ~15, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
        device->CreateBuffer(&desc, nullptr, &cb);
    };
    makeCB(sizeof(glm::mat4) * 3 + sizeof(glm::vec4) * 2 + sizeof(glm::vec4) * 64, m_DX11AOCBuffer);
    makeCB(sizeof(glm::mat4) + sizeof(glm::vec4), m_DX11BlurCBuffer);
    makeCB(sizeof(int) * 4, m_DX11CompositeCBuffer);
    makeCB(sizeof(glm::mat4) + sizeof(glm::vec4) * 5, m_DX11HBAOCBuffer);
    makeCB(sizeof(glm::vec4) * 2 + sizeof(int) + sizeof(float), m_DX11HBAOBlurCBuffer);
    makeCB(sizeof(int) + sizeof(float), m_DX11HBAOCompositeCBuffer);

    // Samplers
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, &m_DX11PointSampler);
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    device->CreateSamplerState(&sampDesc, &m_DX11LinearSampler);

    // Blend state (no blend)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blendDesc, &m_DX11NoBlend);

    // Depth stencil state (depth/ stencil disabled for fullscreen quads)
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;
    device->CreateDepthStencilState(&dsDesc, &m_DX11NoDepth);

    // Generate sample kernel
    m_SampleKernel.clear();
    m_SampleKernel.reserve(64);
    for (int i = 0; i < 64; i++)
    {
        glm::vec3 sample(rnd(gen) * 2.0f - 1.0f, rnd(gen) * 2.0f - 1.0f, rnd(gen));
        sample = glm::normalize(sample) * rnd(gen);
        float scale = (float)i / 64.0f;
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        m_SampleKernel.push_back(sample * scale);
    }

    const bool kernelReady = m_DX11AOPS && m_DX11BlurPS && m_DX11CompositePS &&
        m_DX11AOCBuffer && m_DX11BlurCBuffer && m_DX11CompositeCBuffer;
    const bool hbaoReady = m_DX11HBAORTV && m_DX11HBAOSRV && m_DX11HBAOBlurRTV && m_DX11HBAOBlurSRV &&
        m_DX11HBAOPS && m_DX11HBAOBlurPS && m_DX11HBAOCompositePS &&
        m_DX11HBAOCBuffer && m_DX11HBAOBlurCBuffer && m_DX11HBAOCompositeCBuffer;
    m_DX11Ready = m_DX11AORTV && m_DX11AOSRV && m_DX11BlurRTV && m_DX11BlurSRV &&
        m_DX11OutputRTV && m_DX11OutputSRV && m_DX11VS &&
        (m_Algorithm == Algorithm::SSAO11HBAO ? hbaoReady : kernelReady) &&
        m_DX11PointSampler && m_DX11LinearSampler && m_DX11NoBlend && m_DX11NoDepth;
    if (!m_DX11Ready)
    {
        ERROR("DX11 SSAO resource creation failed; SSAO has been disabled");
        m_Enabled = false;
    }
}

void SSAO::DestroyDX11Resources()
{
    m_DX11Ready = false;
    m_DX11AOTexture.Reset(); m_DX11AORTV.Reset(); m_DX11AOSRV.Reset();
    m_DX11BlurTexture.Reset(); m_DX11BlurRTV.Reset(); m_DX11BlurSRV.Reset();
    m_DX11OutputTexture.Reset(); m_DX11OutputRTV.Reset(); m_DX11OutputSRV.Reset();
    m_DX11HBAOTexture.Reset(); m_DX11HBAORTV.Reset(); m_DX11HBAOSRV.Reset();
    m_DX11HBAOBlurTexture.Reset(); m_DX11HBAOBlurRTV.Reset(); m_DX11HBAOBlurSRV.Reset();
    m_DX11NoiseTexture.Reset(); m_DX11NoiseSRV.Reset();
    m_DX11HBAORandomTexture.Reset(); m_DX11HBAORandomSRV.Reset();
    m_DX11AOCBuffer.Reset(); m_DX11BlurCBuffer.Reset(); m_DX11CompositeCBuffer.Reset();
    m_DX11HBAOCBuffer.Reset(); m_DX11HBAOBlurCBuffer.Reset(); m_DX11HBAOCompositeCBuffer.Reset();
    m_DX11PointSampler.Reset(); m_DX11LinearSampler.Reset();
    m_DX11InputLayout.Reset(); m_DX11NoBlend.Reset(); m_DX11NoDepth.Reset();
}

void SSAO::DX11DrawFullscreenQuad(uint32_t width, uint32_t height, ID3D11SamplerState* sampler)
{
    auto ctx = DX11Context::GetDeviceContext();
    ctx->IASetInputLayout(m_DX11InputLayout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_DX11VS.Get(), nullptr, 0);
    ID3D11SamplerState* activeSampler = sampler ? sampler : m_DX11PointSampler.Get();
    ctx->PSSetSamplers(0, 1, &activeSampler);
    ctx->OMSetBlendState(m_DX11NoBlend.Get(), nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(m_DX11NoDepth.Get(), 0);
    D3D11_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);
    ctx->Draw(3, 0);
}

void SSAO::RenderDX11KernelSSAO(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
    const glm::mat4& view, const glm::mat4& projection)
{
    PROFILE_SCOPE("SSAO.Kernel.Total");
    auto ctx = DX11Context::GetDeviceContext();
    if (!ctx || !m_DX11Ready)
        return;
    glm::mat4 invProj = glm::inverse(projection);

    // Pass 1: AO
    {
        PROFILE_SCOPE("SSAO.Kernel.AO");
        ID3D11RenderTargetView* rtvs[] = { m_DX11AORTV.Get() };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);

        // Map constant buffer
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(m_DX11AOCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        struct AOCB {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 invProj;
            glm::vec4 resolution;
            glm::vec4 params;
            glm::vec4 samples[64];
        }* cb = (AOCB*)mapped.pData;
        cb->view = view;
        cb->proj = projection;
        cb->invProj = invProj;
        cb->resolution = glm::vec4((float)m_DX11AOWidth, (float)m_DX11AOHeight, 0, 0);
        cb->params = glm::vec4(m_Radius, m_Bias, m_Strength, 0);
        for (int i = 0; i < 64 && i < (int)m_SampleKernel.size(); i++)
            cb->samples[i] = glm::vec4(m_SampleKernel[i], 0);
        ctx->Unmap(m_DX11AOCBuffer.Get(), 0);

        ID3D11Buffer* cbs[] = { m_DX11AOCBuffer.Get() };
        ctx->VSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = {
            (ID3D11ShaderResourceView*)depthTexture,
            (ID3D11ShaderResourceView*)normalTexture,
            m_DX11NoiseSRV.Get()
        };
        ctx->PSSetShaderResources(0, 3, srvs);
        ctx->PSSetShader(m_DX11AOPS.Get(), nullptr, 0);
        DX11DrawFullscreenQuad(m_DX11AOWidth, m_DX11AOHeight, m_DX11PointSampler.Get());

        // Unbind SRVs
        ID3D11ShaderResourceView* nullSRVs[3] = {};
        ctx->PSSetShaderResources(0, 3, nullSRVs);
    }

    // Pass 2: Blur
    {
        PROFILE_SCOPE("SSAO.Kernel.Blur");
        ID3D11RenderTargetView* rtvs[] = { m_DX11BlurRTV.Get() };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(m_DX11BlurCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        struct BlurCB {
            glm::mat4 invProj;
            glm::vec4 resolution;
        }* cb = (BlurCB*)mapped.pData;
        cb->invProj = invProj;
        cb->resolution = glm::vec4((float)m_DX11AOWidth, (float)m_DX11AOHeight, 0, 0);
        ctx->Unmap(m_DX11BlurCBuffer.Get(), 0);

        ID3D11Buffer* cbs[] = { m_DX11BlurCBuffer.Get() };
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = { m_DX11AOSRV.Get(), (ID3D11ShaderResourceView*)depthTexture };
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->PSSetShader(m_DX11BlurPS.Get(), nullptr, 0);
        DX11DrawFullscreenQuad(m_DX11AOWidth, m_DX11AOHeight, m_DX11PointSampler.Get());

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx->PSSetShaderResources(0, 2, nullSRVs);
    }

    // Pass 3: Composite
    {
        PROFILE_SCOPE("SSAO.Kernel.Composite");
        ID3D11RenderTargetView* rtvs[] = { m_DX11OutputRTV.Get() };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(m_DX11CompositeCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        *(int*)mapped.pData = m_DebugMode;
        ctx->Unmap(m_DX11CompositeCBuffer.Get(), 0);

        ID3D11Buffer* cbs[] = { m_DX11CompositeCBuffer.Get() };
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = {
            (ID3D11ShaderResourceView*)colorTexture,
            m_DX11BlurSRV.Get()
        };
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->PSSetShader(m_DX11CompositePS.Get(), nullptr, 0);
        DX11DrawFullscreenQuad(m_Width, m_Height, m_DX11LinearSampler.Get());

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx->PSSetShaderResources(0, 2, nullSRVs);
    }
    DX11Context::BindBackBuffer();
}

void SSAO::RenderDX11HBAO(uint64_t colorTexture, uint64_t depthTexture, const glm::mat4& projection)
{
    PROFILE_SCOPE("SSAO.HBAO.Total");
    auto ctx = DX11Context::GetDeviceContext();
    if (!ctx || !m_DX11Ready)
        return;
    glm::mat4 invProj = glm::inverse(projection);
    float focalX = projection[0][0];
    float focalY = projection[1][1];
    float clampedRadius = glm::max(m_Radius, 0.0001f);

    // Pass 1: HBAO
    {
        PROFILE_SCOPE("SSAO.HBAO.AO");
        ID3D11RenderTargetView* rtvs[] = { m_DX11HBAORTV.Get() };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ctx->ClearRenderTargetView(m_DX11HBAORTV.Get(), clearColor);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(m_DX11HBAOCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        struct HBAOCB {
            glm::mat4 invProj;
            glm::vec4 fullRes;
            glm::vec4 invFullRes;
            glm::vec4 focalLen;
            glm::vec4 params;
            glm::vec4 strength;
        }* cb = (HBAOCB*)mapped.pData;
        cb->invProj = invProj;
        cb->fullRes = glm::vec4((float)m_DX11AOWidth, (float)m_DX11AOHeight, 0, 0);
        cb->invFullRes = glm::vec4(1.0f / m_DX11AOWidth, 1.0f / m_DX11AOHeight, 0, 0);
        cb->focalLen = glm::vec4(focalX, focalY, 0, 0);
        cb->params = glm::vec4(clampedRadius, -1.0f / (clampedRadius * clampedRadius), m_MaxRadiusPixels, tanf(glm::radians(m_AngleBiasDegrees)));
        cb->strength = glm::vec4(m_Strength, 0, 0, 0);
        ctx->Unmap(m_DX11HBAOCBuffer.Get(), 0);

        ID3D11Buffer* cbs[] = { m_DX11HBAOCBuffer.Get() };
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = {
            (ID3D11ShaderResourceView*)depthTexture,
            m_DX11HBAORandomSRV.Get()
        };
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->PSSetShader(m_DX11HBAOPS.Get(), nullptr, 0);
        DX11DrawFullscreenQuad(m_DX11AOWidth, m_DX11AOHeight, m_DX11PointSampler.Get());

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx->PSSetShaderResources(0, 2, nullSRVs);
    }

    // Blur passes (horizontal + vertical)
    auto hbaoBlurPass = [&](ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* inputSRV, float dx, float dy)
    {
        PROFILE_SCOPE("SSAO.HBAO.BlurPass");
        ID3D11RenderTargetView* rtvs[] = { rtv };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(m_DX11HBAOBlurCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        struct HBAOBlurCB {
            glm::vec4 invRes;
            glm::vec4 dir;
            int radius;
            float sharpness;
        }* cb = (HBAOBlurCB*)mapped.pData;
        cb->invRes = glm::vec4(1.0f / m_DX11AOWidth, 1.0f / m_DX11AOHeight, 0, 0);
        cb->dir = glm::vec4(dx, dy, 0, 0);
        cb->radius = m_BlurRadius;
        cb->sharpness = m_BlurSharpness;
        ctx->Unmap(m_DX11HBAOBlurCBuffer.Get(), 0);

        ID3D11Buffer* cbs[] = { m_DX11HBAOBlurCBuffer.Get() };
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = { inputSRV };
        ctx->PSSetShaderResources(0, 1, srvs);
        ctx->PSSetShader(m_DX11HBAOBlurPS.Get(), nullptr, 0);
        DX11DrawFullscreenQuad(m_DX11AOWidth, m_DX11AOHeight, m_DX11PointSampler.Get());

        ID3D11ShaderResourceView* nullSRVs[1] = {};
        ctx->PSSetShaderResources(0, 1, nullSRVs);
    };

    hbaoBlurPass(m_DX11HBAOBlurRTV.Get(), m_DX11HBAOSRV.Get(), 1.0f, 0.0f);
    hbaoBlurPass(m_DX11HBAORTV.Get(), m_DX11HBAOBlurSRV.Get(), 0.0f, 1.0f);

    // Final composite
    {
        PROFILE_SCOPE("SSAO.HBAO.Composite");
        ID3D11RenderTargetView* rtvs[] = { m_DX11OutputRTV.Get() };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(m_DX11HBAOCompositeCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return;
        struct HBAOCompositeCB {
            int debugMode;
            float powerExponent;
        }* cb = (HBAOCompositeCB*)mapped.pData;
        cb->debugMode = m_DebugMode;
        cb->powerExponent = m_PowerExponent;
        ctx->Unmap(m_DX11HBAOCompositeCBuffer.Get(), 0);

        ID3D11Buffer* cbs[] = { m_DX11HBAOCompositeCBuffer.Get() };
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = {
            (ID3D11ShaderResourceView*)colorTexture,
            m_DX11HBAOSRV.Get()
        };
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->PSSetShader(m_DX11HBAOCompositePS.Get(), nullptr, 0);
        DX11DrawFullscreenQuad(m_Width, m_Height, m_DX11LinearSampler.Get());

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx->PSSetShaderResources(0, 2, nullSRVs);
    }
    DX11Context::BindBackBuffer();
}
#endif
