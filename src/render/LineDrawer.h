#pragma once
#include "render/D3DContext.h"
#include <d3dcompiler.h>

namespace Render
{
	struct LineVertex
	{
		float pos[4];
		float col[4];
	};

	struct Line
	{
		LineVertex start;
		LineVertex end;
	};

	using LineList = std::vector<Line>;

	constexpr size_t kLineBatchSize = 64;

	class LineDrawer
	{
	public:
		explicit LineDrawer(D3DContext& ctx);
		~LineDrawer();
		LineDrawer(const LineDrawer&) = delete;
		LineDrawer& operator=(const LineDrawer&) = delete;

		void Submit(const LineList& lines) noexcept;

	private:
		D3DContext ctx_;
		ComPtr<ID3D11VertexShader> vs_;
		ComPtr<ID3D11PixelShader> ps_;
		ComPtr<ID3D11InputLayout> inputLayout_;
		ComPtr<ID3D11Buffer> vbo_;

		void CreateResources();
		void DrawBatch(const Line* data, uint32_t count);
	};
}
