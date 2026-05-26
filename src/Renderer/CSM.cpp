#include "stdsfx.h"
#include "CSM.h"
#include <Renderer/RenderCommand.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <Image.h>

#ifdef G_OPENGL
#include <glad/glad.h>
#endif

CSM::CSM(uint32_t cascadeCount, uint32_t shadowMapSize, float splitLambda)
    : m_CascadeCount(cascadeCount), m_ShadowMapSize(shadowMapSize), m_SplitLambda(splitLambda)
{
    m_CascadeDistances.resize(m_CascadeCount + 1);
    m_LightViewProj.resize(m_CascadeCount);

#ifdef G_OPENGL
    // Create shadow map texture array (depth-only)
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_ShadowTextureID);
    glTextureStorage3D(m_ShadowTextureID, 1, GL_DEPTH_COMPONENT32F, m_ShadowMapSize, m_ShadowMapSize, m_CascadeCount);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_ShadowTextureID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glm::vec4 borderColor(1.0f);
    glTextureParameterfv(m_ShadowTextureID, GL_TEXTURE_BORDER_COLOR, &borderColor.x);
    // Manual depth comparison in shader — no GL_COMPARE_REF_TO_TEXTURE

    // Create FBO for shadow rendering
    glCreateFramebuffers(1, &m_ShadowFBO);

    // Create debug color texture for depth visualisation
    glCreateTextures(GL_TEXTURE_2D, 1, &m_DebugTextureID);
    glTextureStorage2D(m_DebugTextureID, 1, GL_RGBA8, m_ShadowMapSize, m_ShadowMapSize);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_DebugTextureID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#endif
}

CSM::~CSM()
{
#ifdef G_OPENGL
    glDeleteTextures(1, &m_ShadowTextureID);
    glDeleteTextures(1, &m_DebugTextureID);
    glDeleteFramebuffers(1, &m_ShadowFBO);
#endif
}

void CSM::Update(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane)
{
    ComputeCascades(view, proj, nearPlane, farPlane);
}

void CSM::BeginShadowPass(uint32_t cascadeIndex)
{
    if (cascadeIndex >= m_CascadeCount) return;
#ifdef G_OPENGL
    // Attach cascade layer to FBO depth attachment
    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowTextureID, 0, cascadeIndex);

    // No color buffer needed
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        ERROR("Shadow FBO incomplete!");

    glViewport(0, 0, m_ShadowMapSize, m_ShadowMapSize);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(m_PolygonOffsetFactor, m_PolygonOffsetUnits);
    glClear(GL_DEPTH_BUFFER_BIT);
#endif
}

void CSM::EndShadowPass()
{
#ifdef G_OPENGL
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
}

void CSM::BindShadowTexture(uint32_t slot) const
{
#ifdef G_OPENGL
    glBindTextureUnit(slot, m_ShadowTextureID);
#endif
}

void CSM::UnbindShadowTexture() const
{
#ifdef G_OPENGL
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
#endif
}

void CSM::SaveShadowMap(const std::string& filepath, uint32_t cascadeIndex) const
{
#ifdef G_OPENGL
    if (cascadeIndex >= m_CascadeCount || !m_ShadowTextureID) return;

    std::vector<float> depthData(m_ShadowMapSize * m_ShadowMapSize);

    // Use glReadPixels via FBO for reliable depth reading
    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowTextureID, 0, cascadeIndex);
    glReadPixels(0, 0, m_ShadowMapSize, m_ShadowMapSize, GL_DEPTH_COMPONENT, GL_FLOAT, depthData.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Diagnostic: print first few values
    float minVal = *std::min_element(depthData.begin(), depthData.end());
    float maxVal = *std::max_element(depthData.begin(), depthData.end());
    TRACE("Shadow cascade {}: min={} max={} first={} ", cascadeIndex, minVal, maxVal, depthData[0]);

    // Normalize depth to [0,255] and save as RGB
    std::vector<unsigned char> imgData(m_ShadowMapSize * m_ShadowMapSize * 3);
    float range = maxVal - minVal;
    if (range < 0.001f) range = 1.0f;

    for (size_t i = 0; i < depthData.size(); i++)
    {
        unsigned char v = (unsigned char)((depthData[i] - minVal) / range * 255.0f);
        imgData[i * 3 + 0] = v;
        imgData[i * 3 + 1] = v;
        imgData[i * 3 + 2] = v;
        // Show shadow vs lit pixels more clearly: red tint for shadowed areas
        if (depthData[i] > 0.5f && depthData[i] < 0.99f)
        {
            imgData[i * 3 + 1] = 0;
        }
    }

    unsigned char* owned = new unsigned char[imgData.size()];
    memcpy(owned, imgData.data(), imgData.size());
    auto saveImg = CreateRef<Image>((int)m_ShadowMapSize, (int)m_ShadowMapSize, 3, owned, PixelType::BYTE);
    saveImg->Save(filepath);
    INFO("Saved shadow map cascade {} to {}", cascadeIndex, filepath);
#endif
}

uint32_t CSM::GetDebugTextureID(uint32_t cascadeIndex)
{
    (void)cascadeIndex;
    return m_DebugTextureID;
}

void CSM::UpdateDebugTexture(uint32_t cascadeIndex)
{
#ifdef G_OPENGL
    if (cascadeIndex >= m_CascadeCount || !m_ShadowTextureID) return;

    std::vector<float> depthData(m_ShadowMapSize * m_ShadowMapSize);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowTextureID, 0, cascadeIndex);
    glReadPixels(0, 0, m_ShadowMapSize, m_ShadowMapSize, GL_DEPTH_COMPONENT, GL_FLOAT, depthData.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::vector<unsigned char> rgba(m_ShadowMapSize * m_ShadowMapSize * 4);
    for (size_t i = 0; i < depthData.size(); i++)
    {
        unsigned char v = (unsigned char)(std::clamp(depthData[i], 0.0f, 1.0f) * 255.0f);
        rgba[i * 4 + 0] = v;
        rgba[i * 4 + 1] = v;
        rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = 255;
    }
    glTextureSubImage2D(m_DebugTextureID, 0, 0, 0, m_ShadowMapSize, m_ShadowMapSize,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
#endif
}

void CSM::ComputeCascades(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane)
{
    farPlane = glm::min(farPlane, m_MaxShadowDistance);
    farPlane = glm::max(farPlane, nearPlane + 1.0f);

    // Compute cascade split distances (practical split: blend of log and uniform)
    for (uint32_t i = 0; i <= m_CascadeCount; i++)
    {
        float p = (float)i / (float)m_CascadeCount;
        float logSplit = nearPlane * powf(farPlane / nearPlane, p);
        float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        m_CascadeDistances[i] = logSplit * m_SplitLambda + uniformSplit * (1.0f - m_SplitLambda);
    }

    glm::mat4 invView = glm::inverse(view);
    float tanHalfFovY = 1.0f / proj[1][1];
    float tanHalfFovX = 1.0f / proj[0][0];

    for (uint32_t i = 0; i < m_CascadeCount; i++)
    {
        float cn = m_CascadeDistances[i];
        float cf = m_CascadeDistances[i + 1];

        // Build the frustum slice directly in view space.
        const glm::vec3 viewCorners[8] = {
            {-cn * tanHalfFovX, -cn * tanHalfFovY, -cn},
            { cn * tanHalfFovX, -cn * tanHalfFovY, -cn},
            { cn * tanHalfFovX,  cn * tanHalfFovY, -cn},
            {-cn * tanHalfFovX,  cn * tanHalfFovY, -cn},
            {-cf * tanHalfFovX, -cf * tanHalfFovY, -cf},
            { cf * tanHalfFovX, -cf * tanHalfFovY, -cf},
            { cf * tanHalfFovX,  cf * tanHalfFovY, -cf},
            {-cf * tanHalfFovX,  cf * tanHalfFovY, -cf},
        };

        glm::vec3 frustumCorners[8];
        glm::vec3 center(0.0f);
        for (int j = 0; j < 8; j++)
        {
            glm::vec4 w = invView * glm::vec4(viewCorners[j], 1.0f);
            w /= w.w;
            frustumCorners[j] = glm::vec3(w);
            center += frustumCorners[j];
        }
        center /= 8.0f;

        glm::vec3 lightDir = glm::normalize(m_Light.Direction);
        glm::vec3 lightPos = center - lightDir * 2000.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0.0f, 1.0f, 0.0f));

        // Compute tight ortho bounds
        float minX = INFINITY, maxX = -INFINITY;
        float minY = INFINITY, maxY = -INFINITY;
        float minZ = INFINITY, maxZ = -INFINITY;

        for (auto& c : frustumCorners)
        {
            glm::vec4 lvp = lightView * glm::vec4(c, 1.0f);
            minX = glm::min(minX, lvp.x); maxX = glm::max(maxX, lvp.x);
            minY = glm::min(minY, lvp.y); maxY = glm::max(maxY, lvp.y);
            minZ = glm::min(minZ, lvp.z); maxZ = glm::max(maxZ, lvp.z);
        }

        // Expand Z to include all potential shadow casters
        float zRange = maxZ - minZ;
        minZ -= zRange * 0.5f;
        maxZ += zRange * 0.5f;

        // glm::ortho expects positive near/far distances along -Z in light view space.
        // In light space, objects in front have negative Z; negate to get distances.
        m_LightViewProj[i] = glm::ortho(minX, maxX, minY, maxY, -maxZ,-minZ) * lightView;
    }
}
