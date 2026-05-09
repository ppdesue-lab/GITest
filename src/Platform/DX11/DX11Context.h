#pragma once

#include <Renderer/GraphicsContext.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

struct GLFWwindow;

class DX11Context : public GraphicsContext
{
public:
	DX11Context(GLFWwindow* windowHandle);
	~DX11Context() override = default;

	void Init() override;
	void SwapBuffers() override;

	static ID3D11Device* GetDevice() { return s_Device.Get(); }
	static ID3D11DeviceContext* GetDeviceContext() { return s_DeviceContext.Get(); }
	static IDXGISwapChain* GetSwapChain() { return s_SwapChain.Get(); }
	static ID3D11RenderTargetView* GetBackBufferRTV() { return s_BackBufferRTV.Get(); }
	static ID3D11DepthStencilView* GetDepthStencilView() { return s_DepthStencilView.Get(); }
	static void ResizeBackBuffer(uint32_t width, uint32_t height);
	static void BindBackBuffer();

private:
	void CreateBackBufferViews(uint32_t width, uint32_t height);

private:
	GLFWwindow* m_WindowHandle = nullptr;

	static Microsoft::WRL::ComPtr<ID3D11Device> s_Device;
	static Microsoft::WRL::ComPtr<ID3D11DeviceContext> s_DeviceContext;
	static Microsoft::WRL::ComPtr<IDXGISwapChain> s_SwapChain;
	static Microsoft::WRL::ComPtr<ID3D11RenderTargetView> s_BackBufferRTV;
	static Microsoft::WRL::ComPtr<ID3D11Texture2D> s_DepthStencilBuffer;
	static Microsoft::WRL::ComPtr<ID3D11DepthStencilView> s_DepthStencilView;
};
