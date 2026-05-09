#include "stdsfx.h"
#include "DX11Context.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

Microsoft::WRL::ComPtr<ID3D11Device> DX11Context::s_Device;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> DX11Context::s_DeviceContext;
Microsoft::WRL::ComPtr<IDXGISwapChain> DX11Context::s_SwapChain;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> DX11Context::s_BackBufferRTV;
Microsoft::WRL::ComPtr<ID3D11Texture2D> DX11Context::s_DepthStencilBuffer;
Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DX11Context::s_DepthStencilView;

DX11Context::DX11Context(GLFWwindow* windowHandle)
	: m_WindowHandle(windowHandle)
{
}

void DX11Context::Init()
{
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(m_WindowHandle, &width, &height);

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = glfwGetWin32Window(m_WindowHandle);
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT flags = 0;
#ifdef _DEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL selectedFeatureLevel;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		flags,
		featureLevels,
		_countof(featureLevels),
		D3D11_SDK_VERSION,
		&swapChainDesc,
		&s_SwapChain,
		&s_Device,
		&selectedFeatureLevel,
		&s_DeviceContext);

	if (FAILED(hr))
		throw std::runtime_error("Failed to create D3D11 device and swap chain");

	CreateBackBufferViews((uint32_t)width, (uint32_t)height);
	BindBackBuffer();

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)width;
	viewport.Height = (float)height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	s_DeviceContext->RSSetViewports(1, &viewport);
}

void DX11Context::SwapBuffers()
{
	s_SwapChain->Present(1, 0);
}

void DX11Context::ResizeBackBuffer(uint32_t width, uint32_t height)
{
	if (!s_SwapChain || width == 0 || height == 0)
		return;

	s_DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	s_BackBufferRTV.Reset();
	s_DepthStencilView.Reset();
	s_DepthStencilBuffer.Reset();

	HRESULT hr = s_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr))
	{
		ERROR("Failed to resize DX11 swap chain");
		return;
	}

	Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
	hr = s_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
	if (FAILED(hr))
	{
		ERROR("Failed to get DX11 back buffer");
		return;
	}
	s_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &s_BackBufferRTV);

	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = width;
	depthDesc.Height = height;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	s_Device->CreateTexture2D(&depthDesc, nullptr, &s_DepthStencilBuffer);
	s_Device->CreateDepthStencilView(s_DepthStencilBuffer.Get(), nullptr, &s_DepthStencilView);
	BindBackBuffer();
}

void DX11Context::BindBackBuffer()
{
	ID3D11RenderTargetView* rtv = s_BackBufferRTV.Get();
	s_DeviceContext->OMSetRenderTargets(1, &rtv, s_DepthStencilView.Get());
}

void DX11Context::CreateBackBufferViews(uint32_t width, uint32_t height)
{
	Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
	HRESULT hr = s_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
	if (FAILED(hr))
		throw std::runtime_error("Failed to get DX11 back buffer");

	hr = s_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &s_BackBufferRTV);
	if (FAILED(hr))
		throw std::runtime_error("Failed to create DX11 render target view");

	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = width;
	depthDesc.Height = height;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	hr = s_Device->CreateTexture2D(&depthDesc, nullptr, &s_DepthStencilBuffer);
	if (FAILED(hr))
		throw std::runtime_error("Failed to create DX11 depth stencil buffer");

	hr = s_Device->CreateDepthStencilView(s_DepthStencilBuffer.Get(), nullptr, &s_DepthStencilView);
	if (FAILED(hr))
		throw std::runtime_error("Failed to create DX11 depth stencil view");
}
