#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace Render
{
	using Microsoft::WRL::ComPtr;

	struct D3DContext
	{
		IDXGISwapChain* swapChain = nullptr;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		float windowWidth = 0.f;
		float windowHeight = 0.f;
	};

	using DrawFunc = std::function<void(D3DContext&)>;

	void InstallHooks();
	void Shutdown();
	D3DContext& GetContext() noexcept;
	bool HasContext() noexcept;
	void OnPresent(DrawFunc&& callback);

	void SetDepthState(D3DContext& ctx, bool writeEnable, bool testEnable, D3D11_COMPARISON_FUNC testFunc);
	void SetBlendState(D3DContext& ctx, bool enable);
}
