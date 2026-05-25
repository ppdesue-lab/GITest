#include "stdsfx.h"
#include "PBRIBL.h"

#ifdef G_OPENGL
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "stb_image.h"

namespace
{
    const glm::vec3 s_SHCoeffs[9] = {
        glm::vec3(0.79f, 0.44f, 0.54f),
        glm::vec3(0.39f, 0.35f, 0.60f),
        glm::vec3(-0.34f, -0.18f, -0.27f),
        glm::vec3(-0.29f, -0.06f, 0.01f),
        glm::vec3(-0.11f, -0.05f, -0.12f),
        glm::vec3(-0.26f, -0.22f, -0.47f),
        glm::vec3(-0.16f, -0.09f, -0.15f),
        glm::vec3(0.56f, 0.21f, 0.14f),
        glm::vec3(0.21f, -0.05f, -0.30f)
    };

    const char* s_CubeVertex = R"(
        #version 410 core
        layout(location = 0) in vec3 a_Position;
        out vec3 v_LocalPos;
        uniform mat4 u_Projection;
        uniform mat4 u_View;
        void main()
        {
            v_LocalPos = a_Position;
            vec4 clip = u_Projection * u_View * vec4(a_Position, 1.0);
            gl_Position = clip.xyww;
        }
    )";

    const char* s_EnvironmentFragment = R"(
        #version 410 core
        in vec3 v_LocalPos;
        layout(location = 0) out vec4 o_Color;
        uniform vec3 u_SHCoeffs[9];
        vec3 EvalSH(vec3 d)
        {
            return max(
                u_SHCoeffs[0] * 0.282095 +
                u_SHCoeffs[1] * (0.488603 * d.y) +
                u_SHCoeffs[2] * (0.488603 * d.z) +
                u_SHCoeffs[3] * (0.488603 * d.x) +
                u_SHCoeffs[4] * (1.092548 * d.x * d.y) +
                u_SHCoeffs[5] * (1.092548 * d.y * d.z) +
                u_SHCoeffs[6] * (0.315392 * (3.0 * d.z * d.z - 1.0)) +
                u_SHCoeffs[7] * (1.092548 * d.x * d.z) +
                u_SHCoeffs[8] * (0.546274 * (d.x * d.x - d.y * d.y)),
                vec3(0.0));
        }
        void main()
        {
            o_Color = vec4(EvalSH(normalize(v_LocalPos)), 1.0);
        }
    )";

    const char* s_EquirectangularFragment = R"(
        #version 410 core
        in vec3 v_LocalPos;
        layout(location = 0) out vec4 o_Color;
        uniform sampler2D u_EquirectangularMap;
        const vec2 INV_ATAN = vec2(0.15915494, 0.31830989);
        vec2 SampleSphericalMap(vec3 d)
        {
            vec2 uv = vec2(atan(d.z, d.x), asin(d.y));
            return uv * INV_ATAN + 0.5;
        }
        void main()
        {
            o_Color = vec4(texture(u_EquirectangularMap, SampleSphericalMap(normalize(v_LocalPos))).rgb, 1.0);
        }
    )";

    const char* s_IrradianceFragment = R"(
        #version 410 core
        in vec3 v_LocalPos;
        layout(location = 0) out vec4 o_Color;
        uniform samplerCube u_EnvironmentMap;
        const float PI = 3.14159265359;
        void main()
        {
            vec3 normal = normalize(v_LocalPos);
            vec3 up = vec3(0.0, 1.0, 0.0);
            vec3 right = normalize(cross(up, normal));
            up = normalize(cross(normal, right));
            vec3 irradiance = vec3(0.0);
            float samples = 0.0;
            for (float phi = 0.0; phi < 2.0 * PI; phi += 0.08)
            {
                for (float theta = 0.0; theta < 0.5 * PI; theta += 0.08)
                {
                    vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
                    vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;
                    irradiance += texture(u_EnvironmentMap, sampleVec).rgb * cos(theta) * sin(theta);
                    samples += 1.0;
                }
            }
            o_Color = vec4(PI * irradiance / samples, 1.0);
        }
    )";

    const char* s_PrefilterFragment = R"(
        #version 410 core
        in vec3 v_LocalPos;
        layout(location = 0) out vec4 o_Color;
        uniform samplerCube u_EnvironmentMap;
        uniform float u_Roughness;
        const float PI = 3.14159265359;
        float RadicalInverseVdC(uint bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return float(bits) * 2.3283064365386963e-10;
        }
        vec2 Hammersley(uint i, uint n)
        {
            return vec2(float(i) / float(n), RadicalInverseVdC(i));
        }
        vec3 ImportanceSampleGGX(vec2 xi, vec3 n, float roughness)
        {
            float a = roughness * roughness;
            float phi = 2.0 * PI * xi.x;
            float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
            float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
            vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
            vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
            vec3 tangent = normalize(cross(up, n));
            vec3 bitangent = cross(n, tangent);
            return normalize(tangent * h.x + bitangent * h.y + n * h.z);
        }
        void main()
        {
            vec3 n = normalize(v_LocalPos);
            vec3 r = n;
            vec3 v = r;
            vec3 prefiltered = vec3(0.0);
            float totalWeight = 0.0;
            const uint SAMPLE_COUNT = 256u;
            for (uint i = 0u; i < SAMPLE_COUNT; ++i)
            {
                vec3 h = ImportanceSampleGGX(Hammersley(i, SAMPLE_COUNT), n, u_Roughness);
                vec3 l = normalize(2.0 * dot(v, h) * h - v);
                float ndotl = max(dot(n, l), 0.0);
                if (ndotl > 0.0)
                {
                    prefiltered += texture(u_EnvironmentMap, l).rgb * ndotl;
                    totalWeight += ndotl;
                }
            }
            o_Color = vec4(prefiltered / max(totalWeight, 0.001), 1.0);
        }
    )";

    const char* s_QuadVertex = R"(
        #version 410 core
        out vec2 v_UV;
        void main()
        {
            vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            v_UV = p;
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
    )";

    const char* s_BRDFFragment = R"(
        #version 410 core
        in vec2 v_UV;
        layout(location = 0) out vec2 o_BRDF;
        const float PI = 3.14159265359;
        float RadicalInverseVdC(uint bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return float(bits) * 2.3283064365386963e-10;
        }
        vec2 Hammersley(uint i, uint n) { return vec2(float(i) / float(n), RadicalInverseVdC(i)); }
        vec3 ImportanceSampleGGX(vec2 xi, vec3 n, float roughness)
        {
            float a = roughness * roughness;
            float phi = 2.0 * PI * xi.x;
            float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
            float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
            return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        }
        float GeometrySchlickGGX(float ndotv, float roughness)
        {
            float a = roughness;
            float k = (a * a) / 2.0;
            return ndotv / (ndotv * (1.0 - k) + k);
        }
        float GeometrySmith(float ndotv, float ndotl, float roughness)
        {
            return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
        }
        vec2 IntegrateBRDF(float ndotv, float roughness)
        {
            vec3 v = vec3(sqrt(1.0 - ndotv * ndotv), 0.0, ndotv);
            float a = 0.0;
            float b = 0.0;
            const uint SAMPLE_COUNT = 512u;
            for (uint i = 0u; i < SAMPLE_COUNT; ++i)
            {
                vec3 h = ImportanceSampleGGX(Hammersley(i, SAMPLE_COUNT), vec3(0.0, 0.0, 1.0), roughness);
                vec3 l = normalize(2.0 * dot(v, h) * h - v);
                float ndotl = max(l.z, 0.0);
                float ndoth = max(h.z, 0.0);
                float vdoth = max(dot(v, h), 0.0);
                if (ndotl > 0.0)
                {
                    float g = GeometrySmith(ndotv, ndotl, roughness);
                    float gVis = (g * vdoth) / max(ndoth * ndotv, 0.001);
                    float fc = pow(1.0 - vdoth, 5.0);
                    a += (1.0 - fc) * gVis;
                    b += fc * gVis;
                }
            }
            return vec2(a, b) / float(SAMPLE_COUNT);
        }
        void main() { o_BRDF = IntegrateBRDF(v_UV.x, v_UV.y); }
    )";
}
#endif

PBRIBL::PBRIBL(const std::string& hdrPath)
{
#ifdef G_OPENGL
    BuildEnvironment(hdrPath);
#else
    (void)hdrPath;
#endif
}

PBRIBL::~PBRIBL()
{
#ifdef G_OPENGL
    glDeleteTextures(1, &m_EnvironmentMap);
    glDeleteTextures(1, &m_IrradianceMap);
    glDeleteTextures(1, &m_PrefilterMap);
    glDeleteTextures(1, &m_BRDFLUT);
    glDeleteFramebuffers(1, &m_CaptureFBO);
    glDeleteVertexArrays(1, &m_CubeVAO);
    glDeleteBuffers(1, &m_CubeVBO);
    glDeleteVertexArrays(1, &m_QuadVAO);
#endif
}

void PBRIBL::BuildEnvironment(const std::string& hdrPath)
{
#ifdef G_OPENGL
    const float cubeVertices[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1,-1, 1,
        -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1,-1, -1,-1, 1, -1, 1, 1,
         1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1,-1,  1,-1, 1,  1, 1, 1,
        -1,-1,-1,  1,-1,-1,  1,-1, 1,  1,-1, 1, -1,-1, 1, -1,-1,-1,
        -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1
    };
    glCreateVertexArrays(1, &m_CubeVAO);
    glCreateBuffers(1, &m_CubeVBO);
    glNamedBufferData(m_CubeVBO, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(m_CubeVAO, 0, m_CubeVBO, 0, sizeof(glm::vec3));
    glEnableVertexArrayAttrib(m_CubeVAO, 0);
    glVertexArrayAttribFormat(m_CubeVAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_CubeVAO, 0, 0);
    glCreateVertexArrays(1, &m_QuadVAO);
    glCreateFramebuffers(1, &m_CaptureFBO);

    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 views[] = {
        glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))
    };

    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_EnvironmentMap);
    glTextureStorage2D(m_EnvironmentMap, 1, GL_RGB16F, 128, 128);
    glTextureParameteri(m_EnvironmentMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_EnvironmentMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_EnvironmentMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_EnvironmentMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_EnvironmentMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    uint32_t hdrTexture = 0;
    if (!hdrPath.empty())
    {
        int hdrWidth = 0;
        int hdrHeight = 0;
        int hdrChannels = 0;
        stbi_set_flip_vertically_on_load(true);
        float* hdrData = stbi_loadf(hdrPath.c_str(), &hdrWidth, &hdrHeight, &hdrChannels, 3);
        if (hdrData)
        {
            glCreateTextures(GL_TEXTURE_2D, 1, &hdrTexture);
            glTextureStorage2D(hdrTexture, 1, GL_RGB16F, hdrWidth, hdrHeight);
            glTextureSubImage2D(hdrTexture, 0, 0, 0, hdrWidth, hdrHeight, GL_RGB, GL_FLOAT, hdrData);
            glTextureParameteri(hdrTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(hdrTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(hdrTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(hdrTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            stbi_image_free(hdrData);
            INFO("Loaded PBR HDR environment: {}", hdrPath);
        }
        else
        {
            ERROR("Failed to load PBR HDR environment: {}", hdrPath);
        }
    }

    auto envShader = Shader::Create("PBREnvironmentCapture", s_CubeVertex,
        hdrTexture ? s_EquirectangularFragment : s_EnvironmentFragment);
    envShader->Bind();
    envShader->SetMat4("u_Projection", projection);
    if (hdrTexture)
    {
        envShader->SetInt("u_EquirectangularMap", 0);
        glBindTextureUnit(0, hdrTexture);
    }
    else
    {
        envShader->SetVec3Array("u_SHCoeffs", (float*)&s_SHCoeffs[0].x, 9);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
    glViewport(0, 0, 128, 128);
    glBindVertexArray(m_CubeVAO);
    for (uint32_t face = 0; face < 6; ++face)
    {
        glNamedFramebufferTextureLayer(m_CaptureFBO, GL_COLOR_ATTACHMENT0, m_EnvironmentMap, 0, face);
        envShader->SetMat4("u_View", views[face]);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    if (hdrTexture)
        glDeleteTextures(1, &hdrTexture);

    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_IrradianceMap);
    glTextureStorage2D(m_IrradianceMap, 1, GL_RGB16F, 32, 32);
    glTextureParameteri(m_IrradianceMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_IrradianceMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_IrradianceMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_IrradianceMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_IrradianceMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    auto irradianceShader = Shader::Create("PBRIrradiance", s_CubeVertex, s_IrradianceFragment);
    irradianceShader->Bind();
    irradianceShader->SetInt("u_EnvironmentMap", 0);
    irradianceShader->SetMat4("u_Projection", projection);
    glBindTextureUnit(0, m_EnvironmentMap);
    glViewport(0, 0, 32, 32);
    for (uint32_t face = 0; face < 6; ++face)
    {
        glNamedFramebufferTextureLayer(m_CaptureFBO, GL_COLOR_ATTACHMENT0, m_IrradianceMap, 0, face);
        irradianceShader->SetMat4("u_View", views[face]);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    const uint32_t maxMipLevels = 5;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_PrefilterMap);
    glTextureStorage2D(m_PrefilterMap, maxMipLevels, GL_RGB16F, 128, 128);
    glTextureParameteri(m_PrefilterMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_PrefilterMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_PrefilterMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_PrefilterMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_PrefilterMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    auto prefilterShader = Shader::Create("PBRPrefilter", s_CubeVertex, s_PrefilterFragment);
    prefilterShader->Bind();
    prefilterShader->SetInt("u_EnvironmentMap", 0);
    prefilterShader->SetMat4("u_Projection", projection);
    glBindTextureUnit(0, m_EnvironmentMap);
    for (uint32_t mip = 0; mip < maxMipLevels; ++mip)
    {
        uint32_t size = 128u >> mip;
        glViewport(0, 0, size, size);
        prefilterShader->SetFloat("u_Roughness", (float)mip / (float)(maxMipLevels - 1));
        for (uint32_t face = 0; face < 6; ++face)
        {
            glNamedFramebufferTextureLayer(m_CaptureFBO, GL_COLOR_ATTACHMENT0, m_PrefilterMap, mip, face);
            prefilterShader->SetMat4("u_View", views[face]);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_BRDFLUT);
    glTextureStorage2D(m_BRDFLUT, 1, GL_RG16F, 512, 512);
    glTextureParameteri(m_BRDFLUT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_BRDFLUT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_BRDFLUT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_BRDFLUT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    auto brdfShader = Shader::Create("PBRBRDFLUT", s_QuadVertex, s_BRDFFragment);
    glNamedFramebufferTexture(m_CaptureFBO, GL_COLOR_ATTACHMENT0, m_BRDFLUT, 0);
    glViewport(0, 0, 512, 512);
    glBindVertexArray(m_QuadVAO);
    brdfShader->Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
    (void)hdrPath;
#endif
}

void PBRIBL::Bind(const Ref<Shader>& shader) const
{
#ifdef G_OPENGL
    if (!shader)
        return;
    shader->SetInt("u_IrradianceMap", 3);
    shader->SetInt("u_PrefilterMap", 4);
    shader->SetInt("u_BRDFLUT", 5);
    glBindTextureUnit(3, m_IrradianceMap);
    glBindTextureUnit(4, m_PrefilterMap);
    glBindTextureUnit(5, m_BRDFLUT);
#else
    (void)shader;
#endif
}

void PBRIBL::BindEnvironment(const Ref<Shader>& shader) const
{
#ifdef G_OPENGL
    if (!shader)
        return;
    shader->SetInt("u_EnvironmentMap", 0);
    glBindTextureUnit(0, m_EnvironmentMap);
#else
    (void)shader;
#endif
}
