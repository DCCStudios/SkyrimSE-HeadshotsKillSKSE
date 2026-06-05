#include "render/DrawHandler.h"
#include "Settings.h"

static uintptr_t g_worldToCamMatrix = RELOCATION_ID(519579, 406126).address();
static RE::NiRect<float>* g_viewPort = (RE::NiRect<float>*)RELOCATION_ID(519618, 406160).address();

static RE::NiAVObject* FindBrowNode(RE::NiAVObject* a_root, const char* a_prefix)
{
	if (!a_root) return nullptr;
	static const char* suffixes[] = {
		" [LLBr]", " [LUBr]", " [RLBr]", " [RUBr]",
		"", nullptr
	};
	char buf[64];
	for (int i = 0; suffixes[i] != nullptr; ++i) {
		snprintf(buf, sizeof(buf), "%s%s", a_prefix, suffixes[i]);
		auto* obj = a_root->GetObjectByName(buf);
		if (obj) return obj;
	}
	for (int idx = 0; idx < 100; ++idx) {
		snprintf(buf, sizeof(buf), "%s [%d]", a_prefix, idx);
		auto* obj = a_root->GetObjectByName(buf);
		if (obj) return obj;
	}
	return nullptr;
}

void DrawHandler::Initialize()
{
	if (initialized_) return;
	if (!Render::HasContext()) return;

	auto& ctx = Render::GetContext();
	lineDrawer_ = std::make_unique<Render::LineDrawer>(ctx);
	initialized_ = true;

	Render::OnPresent([this](Render::D3DContext& ctx) { Render(ctx); });
}

void DrawHandler::Render(Render::D3DContext& ctx)
{
	auto* settings = Settings::GetSingleton();
	if (!settings->enableDebugHitZones) return;

	std::lock_guard<std::recursive_mutex> locker(lock_);

	lines_.clear();
	DrawHitZones();

	if (lines_.empty()) return;

	Render::SetDepthState(ctx, false, false, D3D11_COMPARISON_ALWAYS);
	Render::SetBlendState(ctx, true);
	lineDrawer_->Submit(lines_);
}

void DrawHandler::DrawDebugLine(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_r, float a_g, float a_b, float a_a)
{
	float sx, sy, sz;
	RE::NiCamera::WorldPtToScreenPt3((float(*)[4])g_worldToCamMatrix, *g_viewPort, a_start, sx, sy, sz, 1e-5f);
	if (sz < 0) return;
	sx = (sx - 0.5f) * 2.f;
	sy = (sy - 0.5f) * 2.f;

	float ex, ey, ez;
	RE::NiCamera::WorldPtToScreenPt3((float(*)[4])g_worldToCamMatrix, *g_viewPort, a_end, ex, ey, ez, 1e-5f);
	if (ez < 0) return;
	ex = (ex - 0.5f) * 2.f;
	ey = (ey - 0.5f) * 2.f;

	auto* handler = GetSingleton();
	Render::Line line{};
	line.start.pos[0] = sx; line.start.pos[1] = sy; line.start.pos[2] = sz; line.start.pos[3] = 1.f;
	line.start.col[0] = a_r; line.start.col[1] = a_g; line.start.col[2] = a_b; line.start.col[3] = a_a;
	line.end.pos[0] = ex; line.end.pos[1] = ey; line.end.pos[2] = ez; line.end.pos[3] = 1.f;
	line.end.col[0] = a_r; line.end.col[1] = a_g; line.end.col[2] = a_b; line.end.col[3] = a_a;
	handler->lines_.push_back(line);
}

static void DrawCircle(const RE::NiPoint3& center, const RE::NiPoint3& X, const RE::NiPoint3& Y, float radius, int sides, float r, float g, float b, float a)
{
	const float angleDelta = 2.0f * 3.14159265f / static_cast<float>(sides);
	RE::NiPoint3 lastVert = center + X * radius;

	for (int i = 0; i < sides; i++) {
		float angle = angleDelta * static_cast<float>(i + 1);
		RE::NiPoint3 vert = center + (X * cosf(angle) + Y * sinf(angle)) * radius;
		DrawHandler::DrawDebugLine(lastVert, vert, r, g, b, a);
		lastVert = vert;
	}
}

void DrawHandler::DrawDebugSphere(const RE::NiPoint3& a_center, float a_radius, float a_r, float a_g, float a_b, float a_a)
{
	constexpr int sides = 16;
	constexpr RE::NiPoint3 xAxis{ 1.f, 0.f, 0.f };
	constexpr RE::NiPoint3 yAxis{ 0.f, 1.f, 0.f };
	constexpr RE::NiPoint3 zAxis{ 0.f, 0.f, 1.f };

	DrawCircle(a_center, xAxis, yAxis, a_radius, sides, a_r, a_g, a_b, a_a);
	DrawCircle(a_center, xAxis, zAxis, a_radius, sides, a_r, a_g, a_b, a_a);
	DrawCircle(a_center, yAxis, zAxis, a_radius, sides, a_r, a_g, a_b, a_a);
}

void DrawHandler::DrawHitZones()
{
	auto* settings = Settings::GetSingleton();
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->Get3D()) return;

	RE::NiPoint3 playerPos = player->GetPosition();
	float maxDist = settings->debugHitZoneRadius;
	float maxDistSq = maxDist * maxDist;

	auto* processLists = RE::ProcessLists::GetSingleton();
	if (!processLists) return;

	for (auto& handle : processLists->highActorHandles) {
		auto actorPtr = handle.get();
		if (!actorPtr) continue;
		auto* actor = actorPtr.get();
		if (!actor || actor->IsDead() || !actor->Get3D()) continue;

		RE::NiPoint3 actorPos = actor->GetPosition();
		float dx = actorPos.x - playerPos.x;
		float dy = actorPos.y - playerPos.y;
		float dz = actorPos.z - playerPos.z;
		if (dx * dx + dy * dy + dz * dz > maxDistSq) continue;

		auto* root = actor->Get3D();
		if (!root) continue;

		bool isDragon = actor->HasKeywordString("ActorTypeDragon");

		if (isDragon) {
			auto* headNode = root->GetObjectByName("NPC Head [Head]");
			if (!headNode) headNode = FindBrowNode(root, "NPC Head");

			struct EyeZone { const char* lowerPrefix; const char* upperPrefix; };
			static constexpr EyeZone eyes[] = {
				{ "NPC LLBrow", "NPC LUBrow" },
				{ "NPC RLBrow", "NPC RUBrow" },
			};
			for (auto& eye : eyes) {
				auto* lo = FindBrowNode(root, eye.lowerPrefix);
				auto* hi = FindBrowNode(root, eye.upperPrefix);
				if (!lo || !hi) continue;

				RE::NiPoint3 browMid;
				browMid.x = (lo->world.translate.x + hi->world.translate.x) * 0.5f;
				browMid.y = (lo->world.translate.y + hi->world.translate.y) * 0.5f;
				browMid.z = (lo->world.translate.z + hi->world.translate.z) * 0.5f;

				RE::NiPoint3 center = browMid;
				float radius = settings->dragonEyeHitRadius;

				if (headNode) {
					RE::NiPoint3 outward;
					outward.x = browMid.x - headNode->world.translate.x;
					outward.y = browMid.y - headNode->world.translate.y;
					outward.z = browMid.z - headNode->world.translate.z;
					float len = sqrtf(outward.x * outward.x + outward.y * outward.y + outward.z * outward.z);
					if (len > 0.01f) {
						outward.x /= len;
						outward.y /= len;
						outward.z /= len;
						float offset = radius * 0.6f;
						center.x += outward.x * offset;
						center.y += outward.y * offset;
						center.z += outward.z * offset;
					}
				}

				DrawDebugSphere(center, radius, 1.0f, 0.0f, 1.0f, 0.8f);
			}
		} else {
			const char* headName = "NPC Head [Head]";
			auto* race = actor->GetRace();
			if (race && race->bodyPartData) {
				auto* part = race->bodyPartData->parts[RE::BGSBodyPartDefs::LIMB_ENUM::kHead];
				if (part) {
					const char* n = part->nodeName.c_str();
					if (n && n[0]) headName = n;
				}
			}

			auto* headNode = root->GetObjectByName(headName);
			if (!headNode) continue;

			RE::NiPoint3 headPos = headNode->world.translate;
			float headRadius = 10.0f;
			DrawDebugSphere(headPos, headRadius, 0.0f, 1.0f, 1.0f, 0.8f);
		}
	}
}
