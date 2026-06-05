#include "render/LineDrawer.h"

namespace Render
{
	static const char* kVS = R"(
struct VS_INPUT {
	float4 vPos : POS;
	float4 vColor : COL;
};
struct VS_OUTPUT {
	float4 vPos : SV_POSITION;
	float4 vColor : COLOR0;
};
VS_OUTPUT main(VS_INPUT input) {
	VS_OUTPUT output;
	output.vPos = float4(input.vPos.xyz, 1.0f);
	output.vColor = input.vColor;
	return output;
}
)";

	static const char* kPS = R"(
struct PS_INPUT {
	float4 pos : SV_POSITION;
	float4 color : COLOR0;
};
float4 main(PS_INPUT input) : SV_Target {
	return input.color;
}
)";

	LineDrawer::LineDrawer(D3DContext& ctx) : ctx_(ctx)
	{
		CreateResources();
	}

	LineDrawer::~LineDrawer() = default;

	void LineDrawer::CreateResources()
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;

		ComPtr<ID3DBlob> vsBlob;
		ComPtr<ID3DBlob> errBlob;
		D3DCompile(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_5_0", flags, 0, &vsBlob, &errBlob);
		if (!vsBlob) {
			logger::error("HeadshotsKill: VS compile failed");
			return;
		}
		ctx_.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);

		ComPtr<ID3DBlob> psBlob;
		errBlob.Reset();
		D3DCompile(kPS, strlen(kPS), nullptr, nullptr, nullptr, "main", "ps_5_0", flags, 0, &psBlob, &errBlob);
		if (!psBlob) {
			logger::error("HeadshotsKill: PS compile failed");
			return;
		}
		ctx_.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);

		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		ctx_.device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout_);

		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = static_cast<UINT>(sizeof(LineVertex) * kLineBatchSize * 2);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ctx_.device->CreateBuffer(&bd, nullptr, &vbo_);
	}

	void LineDrawer::Submit(const LineList& lines) noexcept
	{
		if (!vs_ || !ps_ || !vbo_) return;

		ctx_.context->VSSetShader(vs_.Get(), nullptr, 0);
		ctx_.context->PSSetShader(ps_.Get(), nullptr, 0);
		ctx_.context->IASetInputLayout(inputLayout_.Get());
		ctx_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

		UINT stride = sizeof(LineVertex);
		UINT offset = 0;
		ID3D11Buffer* buf = vbo_.Get();
		ctx_.context->IASetVertexBuffers(0, 1, &buf, &stride, &offset);

		size_t idx = 0;
		while (idx < lines.size()) {
			uint32_t count = static_cast<uint32_t>(std::min<size_t>(kLineBatchSize, lines.size() - idx));
			DrawBatch(&lines[idx], count);
			idx += count;
		}
	}

	void LineDrawer::DrawBatch(const Line* data, uint32_t count)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (!SUCCEEDED(ctx_.context->Map(vbo_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		memcpy(mapped.pData, data, sizeof(Line) * count);
		ctx_.context->Unmap(vbo_.Get(), 0);
		ctx_.context->Draw(count * 2, 0);
	}
}
