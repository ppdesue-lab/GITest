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
    m_AOShader = Shader::Create("SSAO", fullscreenVertex, aoFragment);
    m_BlurShader = Shader::Create("SSAOBlur", fullscreenVertex, blurFragment);
    m_CompositeShader = Shader::Create("SSAOComposite", fullscreenVertex, compositeFragment);
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
    Invalidate();
#endif
}

SSAO::~SSAO()
{
#ifdef G_OPENGL
    glDeleteVertexArrays(1, &m_QuadVAO);
    glDeleteTextures(1, &m_NoiseTexture);
    glDeleteFramebuffers(1, &m_AOFBO);
    glDeleteFramebuffers(1, &m_BlurFBO);
    glDeleteFramebuffers(1, &m_OutputFBO);
    glDeleteTextures(1, &m_AOTexture);
    glDeleteTextures(1, &m_BlurTexture);
    glDeleteTextures(1, &m_OutputTexture);
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
        glDeleteTextures(1, &m_AOTexture);
        glDeleteTextures(1, &m_BlurTexture);
        glDeleteTextures(1, &m_OutputTexture);
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
#endif
}

void SSAO::Render(uint64_t colorTexture, uint64_t depthTexture, uint64_t normalTexture,
    const glm::mat4& view, const glm::mat4& projection)
{
#ifdef G_OPENGL
    if (!m_Enabled || !m_AOShader || !m_BlurShader || !m_CompositeShader)
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
