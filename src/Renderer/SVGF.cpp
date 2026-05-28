#include "stdsfx.h"
#include "SVGF.h"

#include <Camera/Camera.h>
#include <Renderer/PathTracer.h>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <tiny_ocl.h>

struct SVGF::Impl
{
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t OutputTexture = 0;
    bool Initialized = false;
    bool HasHistory = false;
    glm::mat4 PreviousViewProjection = glm::mat4(1.0f);

    Scope<tinyocl::Kernel> TemporalKernel;
    Scope<tinyocl::Kernel> AtrousKernel;
    Scope<tinyocl::Kernel> FinalizeKernel;

    Scope<tinyocl::Buffer> CurrentColor;
    Scope<tinyocl::Buffer> CurrentPosition;
    Scope<tinyocl::Buffer> CurrentNormal;
    Scope<tinyocl::Buffer> CurrentAlbedo;
    Scope<tinyocl::Buffer> HistoryColor;
    Scope<tinyocl::Buffer> HistoryMoments;
    Scope<tinyocl::Buffer> HistoryPosition;
    Scope<tinyocl::Buffer> HistoryNormal;
    Scope<tinyocl::Buffer> HistoryAlbedo;
    Scope<tinyocl::Buffer> TemporalColor;
    Scope<tinyocl::Buffer> TemporalMoments;
    Scope<tinyocl::Buffer> PingColor;
    Scope<tinyocl::Buffer> PongColor;
    Scope<tinyocl::Buffer> Pixels;

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

    void CreateBuffers()
    {
        const uint32_t byteCount = Width * Height * static_cast<uint32_t>(sizeof(glm::vec4));
        CurrentColor = CreateScope<tinyocl::Buffer>(byteCount);
        CurrentPosition = CreateScope<tinyocl::Buffer>(byteCount);
        CurrentNormal = CreateScope<tinyocl::Buffer>(byteCount);
        CurrentAlbedo = CreateScope<tinyocl::Buffer>(byteCount);
        HistoryColor = CreateScope<tinyocl::Buffer>(byteCount);
        HistoryMoments = CreateScope<tinyocl::Buffer>(byteCount);
        HistoryPosition = CreateScope<tinyocl::Buffer>(byteCount);
        HistoryNormal = CreateScope<tinyocl::Buffer>(byteCount);
        HistoryAlbedo = CreateScope<tinyocl::Buffer>(byteCount);
        TemporalColor = CreateScope<tinyocl::Buffer>(byteCount);
        TemporalMoments = CreateScope<tinyocl::Buffer>(byteCount);
        PingColor = CreateScope<tinyocl::Buffer>(byteCount);
        PongColor = CreateScope<tinyocl::Buffer>(byteCount);
        Pixels = CreateScope<tinyocl::Buffer>(byteCount);
        ResetHistory();
    }

    bool Initialize()
    {
        if (Initialized)
            return true;
        const std::string kernelPath = GetFilePath("../data/shaders/svgf.cl");
        if (kernelPath.empty())
        {
            ERROR("SVGF OpenCL kernel was not found");
            return false;
        }
        TemporalKernel = CreateScope<tinyocl::Kernel>(kernelPath.c_str(), "TemporalAccumulation");
        AtrousKernel = CreateScope<tinyocl::Kernel>(kernelPath.c_str(), "AtrousFilter");
        FinalizeKernel = CreateScope<tinyocl::Kernel>(kernelPath.c_str(), "FinalizeSVGF");
        Initialized = true;
        CreateBuffers();
        return true;
    }

    void ResetHistory()
    {
        if (HistoryColor) HistoryColor->Clear();
        if (HistoryMoments) HistoryMoments->Clear();
        if (HistoryPosition) HistoryPosition->Clear();
        if (HistoryNormal) HistoryNormal->Clear();
        if (HistoryAlbedo) HistoryAlbedo->Clear();
        HasHistory = false;
    }
};

SVGF::SVGF(uint32_t width, uint32_t height)
    : m_Impl(CreateScope<Impl>())
{
    m_Impl->Width = std::max(width, 1u);
    m_Impl->Height = std::max(height, 1u);
    m_Impl->CreateOutputTexture();
}

SVGF::~SVGF()
{
    if (m_Impl && m_Impl->OutputTexture)
        glDeleteTextures(1, &m_Impl->OutputTexture);
}

void SVGF::Resize(uint32_t width, uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (width == m_Impl->Width && height == m_Impl->Height)
        return;
    m_Impl->Width = width;
    m_Impl->Height = height;
    m_Impl->CreateOutputTexture();
    if (m_Impl->Initialized)
        m_Impl->CreateBuffers();
}

void SVGF::Render(const PathTracer& tracer, const Camera& camera)
{
    if (!m_Enabled || !m_Impl->Initialize())
        return;

    const uint32_t pixelCount = m_Impl->Width * m_Impl->Height;
    if (!tracer.GetRawRadianceBuffer() || !tracer.GetPositionBuffer() ||
        !tracer.GetNormalBuffer() || !tracer.GetAlbedoBuffer())
        return;

    const uint32_t byteCount = pixelCount * static_cast<uint32_t>(sizeof(glm::vec4));
    std::memcpy(m_Impl->CurrentColor->GetHostPtr(), tracer.GetRawRadianceBuffer(), byteCount);
    std::memcpy(m_Impl->CurrentPosition->GetHostPtr(), tracer.GetPositionBuffer(), byteCount);
    std::memcpy(m_Impl->CurrentNormal->GetHostPtr(), tracer.GetNormalBuffer(), byteCount);
    std::memcpy(m_Impl->CurrentAlbedo->GetHostPtr(), tracer.GetAlbedoBuffer(), byteCount);
    m_Impl->CurrentColor->CopyToDevice();
    m_Impl->CurrentPosition->CopyToDevice();
    m_Impl->CurrentNormal->CopyToDevice();
    m_Impl->CurrentAlbedo->CopyToDevice();

    const int hasHistory = m_Impl->HasHistory ? 1 : 0;
    m_Impl->TemporalKernel->SetArguments(
        m_Impl->CurrentColor.get(), m_Impl->CurrentPosition.get(), m_Impl->CurrentNormal.get(), m_Impl->CurrentAlbedo.get(),
        m_Impl->HistoryColor.get(), m_Impl->HistoryMoments.get(), m_Impl->HistoryPosition.get(), m_Impl->HistoryNormal.get(),
        m_Impl->HistoryAlbedo.get(), m_Impl->Width, m_Impl->Height, m_HistoryAlpha,
        m_Impl->PreviousViewProjection, hasHistory, m_Impl->TemporalColor.get(), m_Impl->TemporalMoments.get());
    m_Impl->TemporalKernel->Run(pixelCount);

    tinyocl::Buffer* input = m_Impl->TemporalColor.get();
    tinyocl::Buffer* output = m_Impl->PingColor.get();
    const int iterationCount = glm::clamp(m_Iterations, 0, 6);
    for (int i = 0; i < iterationCount; ++i)
    {
        const int stepSize = 1 << i;
        m_Impl->AtrousKernel->SetArguments(
            input, m_Impl->CurrentPosition.get(), m_Impl->CurrentNormal.get(), m_Impl->CurrentAlbedo.get(),
            m_Impl->TemporalMoments.get(), m_Impl->Width, m_Impl->Height, stepSize,
            m_PhiColor, m_PhiNormal, m_PhiDepth, output);
        m_Impl->AtrousKernel->Run(pixelCount);
        input = output;
        output = (output == m_Impl->PingColor.get()) ? m_Impl->PongColor.get() : m_Impl->PingColor.get();
    }

    m_Impl->FinalizeKernel->SetArguments(
        input, m_Impl->CurrentPosition.get(), m_Impl->CurrentNormal.get(), m_Impl->CurrentAlbedo.get(),
        m_Impl->TemporalMoments.get(), m_Impl->Width, m_Impl->Height,
        m_Impl->HistoryColor.get(), m_Impl->HistoryMoments.get(), m_Impl->HistoryPosition.get(),
        m_Impl->HistoryNormal.get(), m_Impl->HistoryAlbedo.get(), m_Impl->Pixels.get());
    m_Impl->FinalizeKernel->Run(pixelCount);
    m_Impl->Pixels->CopyFromDevice();
    glTextureSubImage2D(m_Impl->OutputTexture, 0, 0, 0, m_Impl->Width, m_Impl->Height, GL_RGBA, GL_FLOAT,
        m_Impl->Pixels->GetHostPtr());

    m_Impl->PreviousViewProjection = camera.GetProjectionMatrix() * camera.GetViewMatrix();
    m_Impl->HasHistory = true;
}

void SVGF::ResetHistory()
{
    m_Impl->ResetHistory();
}

uint64_t SVGF::GetOutputTexture() const
{
    return m_Impl->OutputTexture;
}

bool SVGF::HasHistory() const
{
    return m_Impl->HasHistory;
}
