#pragma once

#include <base.h>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

class Camera;
class Scene;

class PathTracer
{
public:
    PathTracer(uint32_t width, uint32_t height, const std::string& environmentPath);
    ~PathTracer();

    void Resize(uint32_t width, uint32_t height);
    void Render(const Scene& scene, const Camera& camera);
    void MarkSceneDirty();
    void ResetAccumulation();

    uint64_t GetOutputTexture() const;
    const glm::vec4* GetRawRadianceBuffer() const;
    const glm::vec4* GetPositionBuffer() const;
    const glm::vec4* GetNormalBuffer() const;
    const glm::vec4* GetAlbedoBuffer() const;
    uint32_t GetSampleCount() const;
    uint32_t GetTriangleCount() const;
    const std::string& GetStatus() const;

private:
    struct Impl;
    Scope<Impl> m_Impl;
};
