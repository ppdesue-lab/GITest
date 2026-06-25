#include "stdsfx.h"
#include "SSAO.h"

#include <random>

#ifdef G_OPENGL
#include <glad/glad.h>
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
    glTextureStorage2D(m_AOTexture, 1, GL_R16F, m_Width, m_Height);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_AOTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_AOFBO, GL_COLOR_ATTACHMENT0, m_AOTexture, 0);
    if (glCheckNamedFramebufferStatus(m_AOFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO framebuffer is incomplete!");

    glCreateFramebuffers(1, &m_BlurFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_BlurTexture);
    glTextureStorage2D(m_BlurTexture, 1, GL_R16F, m_Width, m_Height);
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
    glTextureStorage2D(m_HBAOTexture, 1, GL_RG16F, m_Width, m_Height);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_HBAOTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_HBAOFBO, GL_COLOR_ATTACHMENT0, m_HBAOTexture, 0);
    if (glCheckNamedFramebufferStatus(m_HBAOFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO11 HBAO framebuffer is incomplete!");

    glCreateFramebuffers(1, &m_HBAOBlurFBO);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_HBAOBlurTexture);
    glTextureStorage2D(m_HBAOBlurTexture, 1, GL_RG16F, m_Width, m_Height);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_HBAOBlurTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_HBAOBlurFBO, GL_COLOR_ATTACHMENT0, m_HBAOBlurTexture, 0);
    if (glCheckNamedFramebufferStatus(m_HBAOBlurFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ERROR("SSAO11 HBAO blur framebuffer is incomplete!");
#endif
}

void SSAO::Render(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
    const glm::mat4& view, const glm::mat4& projection)
{
#ifdef G_OPENGL
    if (!m_Enabled)
        return;
    if (m_Algorithm == Algorithm::SSAO11HBAO)
        RenderHBAO(colorTexture, depthTexture, projection);
    else
        RenderKernelSSAO(colorTexture, depthTexture, normalTexture, view, projection);
#else
    (void)colorTexture;
    (void)depthTexture;
    (void)normalTexture;
    (void)view;
    (void)projection;
#endif
}

uint64_t SSAO::GetAOTexture() const
{
    return m_Algorithm == Algorithm::SSAO11HBAO ? m_HBAOTexture : m_BlurTexture;
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
    glViewport(0, 0, m_Width, m_Height);
    m_AOShader->Bind();
    m_AOShader->SetInt("u_Depth", 0);
    m_AOShader->SetInt("u_Normal", 1);
    m_AOShader->SetInt("u_Noise", 2);
    m_AOShader->SetMat4("u_View", view);
    m_AOShader->SetMat4("u_Projection", projection);
    m_AOShader->SetMat4("u_InverseProjection", glm::inverse(projection));
    m_AOShader->SetFloat2("u_Resolution", glm::vec2(m_Width, m_Height));
    m_AOShader->SetFloat("u_Radius", m_Radius);
    m_AOShader->SetFloat("u_Bias", m_Bias);
    m_AOShader->SetFloat("u_Strength", m_Strength);
    m_AOShader->SetVec3Array("u_Samples", &m_SampleKernel[0].x, (uint32_t)m_SampleKernel.size());
    glBindTextureUnit(0, (uint32_t)depthTexture);
    glBindTextureUnit(1, (uint32_t)normalTexture);
    glBindTextureUnit(2, m_NoiseTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, m_BlurFBO);
    m_BlurShader->Bind();
    m_BlurShader->SetInt("u_AO", 0);
    m_BlurShader->SetInt("u_Depth", 1);
    m_BlurShader->SetMat4("u_InverseProjection", glm::inverse(projection));
    m_BlurShader->SetFloat2("u_Resolution", glm::vec2(m_Width, m_Height));
    glBindTextureUnit(0, m_AOTexture);
    glBindTextureUnit(1, (uint32_t)depthTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
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
    glViewport(0, 0, m_Width, m_Height);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_HBAOShader->Bind();
    m_HBAOShader->SetInt("u_Depth", 0);
    m_HBAOShader->SetInt("u_Random", 1);
    m_HBAOShader->SetMat4("u_InverseProjection", inverseProjection);
    m_HBAOShader->SetFloat2("u_FullResolution", glm::vec2(m_Width, m_Height));
    m_HBAOShader->SetFloat2("u_InvFullResolution", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
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
    glViewport(0, 0, m_Width, m_Height);
    m_HBAOBlurShader->Bind();
    m_HBAOBlurShader->SetInt("u_AODepth", 0);
    m_HBAOBlurShader->SetFloat2("u_InvResolution", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
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
