#include "Hooks.h"
#include "HeadshotLogic.h"
#include "Offsets.h"
#include "PlayerHelmetTracker.h"
#include "Settings.h"

#include <Windows.h>
#include <unordered_set>

namespace Hooks
{
	namespace
	{
		struct PendingCandidate
		{
			std::uint32_t aggressorFormID;
			std::uint32_t targetFormID;
			std::uint8_t  category;
			bool          headConfirmedByBone;  ///< bone-name/geometric detection said head
			std::uint64_t expireQpc;
		};

		std::vector<PendingCandidate> g_pending{};
		std::mutex                    g_mutex{};
		std::uint64_t                 g_qpcFreq{ 1 };

		// Actors whose helmets were knocked off by us. Keyed by FormID.
		// Entries are checked live: if the actor has re-equipped head armor they
		// are removed automatically, so no expiry needed.
		std::unordered_set<RE::FormID> g_bareHeads{};
		std::mutex                     g_bareHeadsMutex{};

		std::uint64_t QpcNow()
		{
			LARGE_INTEGER c;
			QueryPerformanceCounter(&c);
			return static_cast<std::uint64_t>(c.QuadPart);
		}

		struct ProjectileImpactHook
		{
			static bool thunk(RE::Projectile* a_projectile)
			{
				if (a_projectile) {
					HeadshotLogic::OnProjectileImpact(a_projectile);
				}
				bool result = func(a_projectile);

				// Post-processing: force arrows to stick on bare heads after
				// Ricochet Framework / CIF have had their say.
				if (a_projectile) {
					HeadshotLogic::PostImpactStickFix(a_projectile);
				}
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct HandleProjectileAttackHook
		{
			static void* thunk(RE::Character* a_character, RE::HitData* a_hitData)
			{
				HeadshotLogic::EvaluateHitData(a_character, a_hitData);
				return func(a_character, a_hitData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}
}

void Hooks::PushPendingCandidate(std::uint32_t a_aggressorFormID, std::uint32_t a_targetFormID,
	std::uint8_t a_category, bool a_headByBone)
{
	if (a_targetFormID == 0) return;

	const auto now = QpcNow();
	std::lock_guard lock(g_mutex);
	g_pending.push_back(PendingCandidate{
		.aggressorFormID    = a_aggressorFormID,
		.targetFormID       = a_targetFormID,
		.category           = a_category,
		.headConfirmedByBone = a_headByBone,
		.expireQpc          = now + g_qpcFreq * 2,
	});
}

bool Hooks::ConsumePendingCandidate(std::uint32_t a_aggressorFormID, std::uint32_t a_targetFormID,
	std::uint8_t& a_outCategory, bool& a_outHeadByBone)
{
	if (a_targetFormID == 0) return false;

	const auto now = QpcNow();
	std::lock_guard lock(g_mutex);

	for (auto it = g_pending.begin(); it != g_pending.end();) {
		if (it->expireQpc <= now) {
			it = g_pending.erase(it);
			continue;
		}
		if (it->targetFormID != a_targetFormID) {
			++it;
			continue;
		}
		if (it->aggressorFormID != 0 && it->aggressorFormID != a_aggressorFormID) {
			++it;
			continue;
		}
		a_outCategory    = it->category;
		a_outHeadByBone  = it->headConfirmedByBone;
		it = g_pending.erase(it);
		return true;
	}
	return false;
}

void Hooks::RegisterBareHead(RE::FormID a_actorFormID)
{
	if (!a_actorFormID) return;
	std::lock_guard lock(g_bareHeadsMutex);
	g_bareHeads.insert(a_actorFormID);
}

bool Hooks::IsBareHead(RE::Actor* a_actor)
{
	if (!a_actor) return false;
	{
		std::lock_guard lock(g_bareHeadsMutex);
		if (!g_bareHeads.count(a_actor->GetFormID())) {
			return false;
		}
	}

	// Auto-clear if the actor has re-equipped protective head armor (rating > 0).
	auto* changes = a_actor->GetInventoryChanges();
	if (changes && changes->entryList) {
		for (auto* entry : *changes->entryList) {
			if (!entry || !entry->object || !entry->IsWorn()) continue;
			auto* ar = entry->object->As<RE::TESObjectARMO>();
			if (!ar) continue;
			const bool coversHead =
				ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHead) ||
				ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHair);
			if (!coversHead || ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kBody)) continue;
			if (ar->GetArmorRating() > 0) {
				std::lock_guard lock(g_bareHeadsMutex);
				g_bareHeads.erase(a_actor->GetFormID());
				return false;
			}
		}
	}

	return true;
}

void Hooks::ClearBareHeads()
{
	std::lock_guard lock(g_bareHeadsMutex);
	g_bareHeads.clear();
}

void Hooks::Install()
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_qpcFreq = static_cast<std::uint64_t>(std::max<std::int64_t>(f.QuadPart, 1));

	{
		auto hook = Reloc::Address(RelocId::ProjectileImpact);
		if (*reinterpret_cast<std::uint16_t*>(hook.address()) != 0x90FF) {
			logger::critical("HeadshotsKill: ProjectileImpact site mismatch");
			stl::report_and_fail("HeadshotsKill: ProjectileImpact hook failed"sv);
		}
		REL::safe_fill(hook.address(), 0x90, 6);
		WriteThunkCall5<ProjectileImpactHook>(hook.address());
	}

	{
		auto hook = Reloc::Address(RelocId::HandleProjectileAttack);
		if (*reinterpret_cast<std::uint8_t*>(hook.address()) != 0xE8) {
			logger::critical("HeadshotsKill: HandleProjectileAttack site mismatch");
			stl::report_and_fail("HeadshotsKill: HandleProjectileAttack hook failed"sv);
		}
		WriteThunkCall5<HandleProjectileAttackHook>(hook.address());
	}

	logger::info("HeadshotsKill: hooks installed");
}

void Hooks::StartPeriodicTrackerUpdate()
{
	static std::atomic<bool> running{ false };
	if (running.exchange(true)) return;

	std::thread([]() {
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			auto* tracker = PlayerHelmetTracker::GetSingleton();
			if (!tracker->IsTracking()) {
				running.store(false);
				return;
			}
			SKSE::GetTaskInterface()->AddTask([]() {
				auto* tracker = PlayerHelmetTracker::GetSingleton();
				if (tracker->IsTracking()) {
					tracker->Update();
				}
			});
		}
	}).detach();
}
