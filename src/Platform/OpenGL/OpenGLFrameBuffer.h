#pragma once
#include "Renderer/FrameBuffer.h"

class OpenGLFrameBuffer : public FrameBuffer
{
public:
	OpenGLFrameBuffer(const FrameBufferSpecification& spec);
    virtual ~OpenGLFrameBuffer();

    void Invalidate();

    virtual void Bind() override;
	virtual void Unbind() override;

	virtual void Resize(uint32_t width, uint32_t height) override;
	virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) override;
    virtual void Save2File(const std::string& filename, uint32_t attachmentIndex) override;

	virtual void ClearAttachment(uint32_t attachmentIndex, int value) override;
	virtual uint32_t GetColorAttachmentRendererID(uint32_t index =0) const override;
	
	//get information about this framebuffer
	virtual const FrameBufferSpecification& GetSpecification() const override;
    
private:
	uint32_t m_RendererID = 0;
    FrameBufferSpecification m_Specification;

    std::vector<FrameBufferTextureSpecification> m_ColorAttachmentSpecfications;
    FrameBufferTextureSpecification m_DepthAttachmentSpecification = FrameBufferTextureFormat::None;
    
    //opengl stuff
    std::vector<uint32_t> m_ColorAttachments;
    uint32_t m_DepthAttachment;
};
