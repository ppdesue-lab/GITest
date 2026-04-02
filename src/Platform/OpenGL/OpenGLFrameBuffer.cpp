#include "stdsfx.h"
#include "OpenGLFrameBuffer.h"
#include <glad/glad.h>

namespace Utils
{
	static  const uint32_t s_MaxFrameBufferSize = 8192;

	static GLenum TextureTarget(bool multisampled)
	{
		return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
	}

	static void BindTexture(bool multisampled, uint32_t id)
	{
		glBindTexture(TextureTarget(multisampled), id);
	}

	static bool IsDepthFormat(FrameBufferTextureFormat format)
	{
		switch (format)
		{
		case FrameBufferTextureFormat::None:
			return false;
		case FrameBufferTextureFormat::RGBA8:
			return false;
		case FrameBufferTextureFormat::RED_INTEGER:
			return false;
		case FrameBufferTextureFormat::Depth24Stencil8:
			return true;
		}
		return false;
	}

	static void CreateTextures(bool multisampled, uint32_t* outID, uint32_t count)
	{
		glCreateTextures(TextureTarget(multisampled), count, outID);
	}

	static void AttachDepthTexture(uint32_t id,int samples, GLenum internalFormat, uint32_t width, uint32_t height)
	{
		bool multisampled = samples > 1;
		if (multisampled)
		{
			glTextureStorage2DMultisample(id, samples, internalFormat, width, height, GL_FALSE);
		}
		else
		{
			glTextureStorage2D(id, 1, internalFormat, width, height);
			glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, TextureTarget(multisampled), id, 0);
	}

	static void AttachColorTexture(uint32_t id, int samples, GLenum internalFormat, GLenum format, uint32_t width, uint32_t height, int index)
	{
		bool multisampled = samples > 1;
		if (multisampled)
		{
			glTextureStorage2DMultisample(id, samples, internalFormat, width, height, GL_FALSE);
		}
		else
		{
			glTextureStorage2D(id, 1, internalFormat, width, height);
			glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, TextureTarget(multisampled), id, 0);
	}

	static GLenum FBTextureFormatToGL(FrameBufferTextureFormat format)
	{
		switch (format)
		{
		case FrameBufferTextureFormat::None:
			return GL_NONE;
		case FrameBufferTextureFormat::RGBA8:
			return GL_RGBA8;
		case FrameBufferTextureFormat::RED_INTEGER:
			return GL_RED_INTEGER;
		case FrameBufferTextureFormat::Depth24Stencil8:
			return GL_DEPTH24_STENCIL8;
		}
		ERROR("Unknown FrameBufferTextureFormat!");
		return 0;
	}
}
OpenGLFrameBuffer::OpenGLFrameBuffer(const FrameBufferSpecification& spec)
	: m_Specification(spec)
{
	for (auto spec : m_Specification.Attachments.Attachments)
	{
		if (!Utils::IsDepthFormat(spec.TextureFormat))
			m_ColorAttachmentSpecfications.push_back(spec);
		else
			m_DepthAttachmentSpecification = spec;
	}
	Invalidate();
}

OpenGLFrameBuffer::~OpenGLFrameBuffer()
{
	glDeleteFramebuffers(1, &m_RendererID);
	glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
	glDeleteTextures(1, &m_DepthAttachment);
}

void OpenGLFrameBuffer::Invalidate()
{
	if (m_RendererID)
	{
		//release old textures
		glDeleteFramebuffers(1, &m_RendererID);
		glDeleteTextures((GLsizei)m_ColorAttachments.size(), m_ColorAttachments.data());
		glDeleteTextures(1, &m_DepthAttachment);

		m_ColorAttachments.clear();
		m_DepthAttachment = 0;
	}

	//create new textures

	glCreateFramebuffers(1, &m_RendererID);
	glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
	bool multisample = m_Specification.Samples > 1;
	//attachments
	if (m_ColorAttachmentSpecfications.size())
	{
		m_ColorAttachments.resize(m_ColorAttachmentSpecfications.size());
		Utils::CreateTextures(multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, m_ColorAttachments.data(), (GLsizei)m_ColorAttachments.size());
		for (size_t i = 0; i < m_ColorAttachments.size(); i++)
		{
			Utils::BindTexture(multisample, m_ColorAttachments[i]);
			switch (m_ColorAttachmentSpecfications[i].TextureFormat)
			{
			case FrameBufferTextureFormat::RGBA8:
				Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples, GL_RGBA8, GL_RGBA, m_Specification.Width, m_Specification.Height, i);
				break;
			case FrameBufferTextureFormat::RED_INTEGER:
				Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples, GL_R32I, GL_RED_INTEGER, m_Specification.Width, m_Specification.Height, i);
				break;
			}
			
		}
	}
	if (m_DepthAttachmentSpecification.TextureFormat != FrameBufferTextureFormat::None)
	{
		Utils::CreateTextures(multisample, &m_DepthAttachment, 1);
		Utils::BindTexture(multisample, m_DepthAttachment);
		switch (m_DepthAttachmentSpecification.TextureFormat)
		{
		case FrameBufferTextureFormat::Depth24Stencil8:
			Utils::AttachDepthTexture(m_DepthAttachment, m_Specification.Samples, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
			break;
		}
	}

	if (m_ColorAttachments.size() > 1)
	{
		GLenum* buffers = new GLenum[m_ColorAttachments.size()];
		for (size_t i = 0; i < m_ColorAttachments.size(); i++)
			buffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
		glDrawBuffers((GLsizei)m_ColorAttachments.size(), buffers);
		delete[] buffers;
	}
	else if (m_ColorAttachments.empty())
	{
		glDrawBuffer(GL_NONE);
	}

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		ERROR("FrameBuffer is incomplete!");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void OpenGLFrameBuffer::Bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
	glViewport(0, 0, m_Specification.Width, m_Specification.Height);
}

void OpenGLFrameBuffer::Unbind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFrameBuffer::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
	{
		WARN("Attempted to resize framebuffer to {0}, {1}", width, height);
		return;
	}
	m_Specification.Width = width;
	m_Specification.Height = height;
	Invalidate();
}

int OpenGLFrameBuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
{
	if (attachmentIndex >= m_ColorAttachments.size())
	{
		ERROR("Attachment index out of bounds!");
		return -1;
	}
	glReadBuffer(GL_COLOR_ATTACHMENT0 + (GLenum)attachmentIndex);
	int pixelData;
	glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
	return pixelData;
}

void OpenGLFrameBuffer::ClearAttachment(uint32_t attachmentIndex, int value)
{
	if (attachmentIndex >= m_ColorAttachments.size())
	{
		ERROR("Attachment index out of bounds!");
		return;
	}
	auto& spec = m_ColorAttachmentSpecfications[attachmentIndex];
	GLenum format = Utils::FBTextureFormatToGL(spec.TextureFormat);
	glClearTexImage(m_ColorAttachments[attachmentIndex], 0, format, GL_INT, &value);
}

uint32_t OpenGLFrameBuffer::GetColorAttachmentRendererID(uint32_t index) const
{
	if (index >= m_ColorAttachments.size())
	{
		ERROR("Attachment index out of bounds!");
		return 0;
	}
	return m_ColorAttachments[index];
}

const FrameBufferSpecification& OpenGLFrameBuffer::GetSpecification() const
{
	return m_Specification;
}

