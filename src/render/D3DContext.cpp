#include "render/D3DContext.h"

using Microsoft::WRL::ComPtr;

static Render::D3DContext gameContext;
static std::vector<Render::DrawFunc> presentCallbacks;
static bool initialized = false;
static ComPtr<ID3D11DepthStencilState> noDepthState;
static ComPtr<ID3D11BlendState> alphaBlendState;

static REL::Relocation<HRESULT(IDXGISwapChain*, UINT, UINT)> fnPresentOrig;

static HRESULT Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
	ComPtr<ID3D11DepthStencilState> savedDS;
	uint32_t savedStencilRef;
	gameContext.context->OMGetDepthStencilState(&savedDS, &savedStencilRef);

	ComPtr<ID3D11BlendState> savedBlend;
	float savedBlendFactors[4];
	uint32_t savedSampleMask;
	gameContext.context->OMGetBlendState(&savedBlend, savedBlendFactors, &savedSampleMask);

	D3D11_VIEWPORT vp;
	uint32_t numVP = 1;
	gameContext.context->RSGetViewports(&numVP, &vp);

	ComPtr<ID3D11RasterizerState> savedRaster;
	gameContext.context->RSGetState(&savedRaster);

	ComPtr<ID3D11RenderTargetView> cachedRTV;
	ComPtr<ID3D11DepthStencilView> cachedDSV;
	gameContext.context->OMGetRenderTargets(1, &cachedRTV, &cachedDSV);

	for (auto& cb : presentCallbacks)
		cb(gameContext);

	ID3D11RenderTargetView* rtv = cachedRTV.Get();
	gameContext.context->OMSetRenderTargets(1, &rtv, cachedDSV.Get());
	gameContext.context->RSSetState(savedRaster.Get());
	gameContext.context->RSSetViewports(1, &vp);
	gameContext.context->OMSetBlendState(savedBlend.Get(), savedBlendFactors, savedSampleMask);
	gameContext.context->OMSetDepthStencilState(savedDS.Get(), savedStencilRef);

	return fnPresentOrig(swapChain, syncInterval, flags);
}

static bool ReadSwapChain()
{
	__try {
		struct UnkCreationD3D
		{
			uintptr_t unk0;
			uintptr_t unk1;
			uintptr_t unk2;
			IDXGISwapChain* swapChain;
		};
		auto data = **(UnkCreationD3D**)(RELOCATION_ID(524730, 411349).address());
		gameContext.swapChain = data.swapChain;

		DXGI_SWAP_CHAIN_DESC desc;
		if (!SUCCEEDED(data.swapChain->GetDesc(&desc)))
			return false;

		RECT r;
		GetClientRect(desc.OutputWindow, &r);
		gameContext.windowWidth = static_cast<float>(r.right);
		gameContext.windowHeight = static_cast<float>(r.bottom);
	} __except (1) {
		return false;
	}
	return true;
}

void Render::InstallHooks()
{
	if (initialized) return;

	if (!ReadSwapChain()) {
		logger::error("HeadshotsKill: Failed to hook IDXGISwapChain::Present - debug draw disabled.");
		return;
	}

	if (!SUCCEEDED(gameContext.swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(gameContext.device.GetAddressOf())))) {
		logger::error("HeadshotsKill: Failed to get D3D11 device.");
		return;
	}

	REL::Relocation<std::uintptr_t> D3DVtbl{ *(uintptr_t*)gameContext.swapChain };
	fnPresentOrig = D3DVtbl.write_vfunc(0x8, Present);

	gameContext.device->GetImmediateContext(&gameContext.context);

	D3D11_DEPTH_STENCIL_DESC dsDesc{};
	dsDesc.DepthEnable = FALSE;
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	dsDesc.StencilEnable = FALSE;
	gameContext.device->CreateDepthStencilState(&dsDesc, &noDepthState);

	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	gameContext.device->CreateBlendState(&blendDesc, &alphaBlendState);

	initialized = true;
}

void Render::Shutdown()
{
	if (!initialized) return;
	initialized = false;
	noDepthState.Reset();
	alphaBlendState.Reset();
	gameContext.context.Reset();
	gameContext.device.Reset();
}

Render::D3DContext& Render::GetContext() noexcept
{
	return gameContext;
}

bool Render::HasContext() noexcept
{
	return initialized;
}

void Render::OnPresent(DrawFunc&& callback)
{
	presentCallbacks.emplace_back(std::move(callback));
}

void Render::SetDepthState(D3DContext& ctx, bool, bool, D3D11_COMPARISON_FUNC)
{
	ctx.context->OMSetDepthStencilState(noDepthState.Get(), 0);
}

void Render::SetBlendState(D3DContext& ctx, bool enable)
{
	if (enable) {
		float factors[4] = { 1.f, 1.f, 1.f, 1.f };
		ctx.context->OMSetBlendState(alphaBlendState.Get(), factors, 0xffffffff);
	} else {
		ctx.context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	}
}
