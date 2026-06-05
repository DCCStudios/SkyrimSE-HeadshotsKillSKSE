#pragma once
#include "render/LineDrawer.h"

class DrawHandler
{
public:
	static DrawHandler* GetSingleton()
	{
		static DrawHandler singleton;
		return std::addressof(singleton);
	}

	void Initialize();
	void Render(Render::D3DContext& ctx);

	static void DrawDebugLine(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_r, float a_g, float a_b, float a_a);
	static void DrawDebugSphere(const RE::NiPoint3& a_center, float a_radius, float a_r, float a_g, float a_b, float a_a);

	void DrawHitZones();

private:
	DrawHandler() = default;
	DrawHandler(const DrawHandler&) = delete;
	DrawHandler& operator=(const DrawHandler&) = delete;

	bool initialized_ = false;
	std::unique_ptr<Render::LineDrawer> lineDrawer_;
	std::recursive_mutex lock_;

	Render::LineList lines_;
};
