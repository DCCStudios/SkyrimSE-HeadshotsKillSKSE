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
				__try {
					if (a_projectile) {
						HeadshotLogic::OnProjectileImpact(a_projectile);
					}
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					static std::uint64_t lastLogTick = 0;
					auto now = QpcNow();
					if (now - lastLogTick > 10000000ULL) {
						logger::error("HeadshotsKill: exception in OnProjectileImpact (code=0x{:X})",
							static_cast<unsigned>(GetExceptionCode()));
						lastLogTick = now;
					}
				}

				bool result = func(a_projectile);

				__try {
					if (a_projectile) {
						HeadshotLogic::PostImpactStickFix(a_projectile);
					}
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					static std::uint64_t lastLogTick2 = 0;
					auto now = QpcNow();
					if (now - lastLogTick2 > 10000000ULL) {
						logger::error("HeadshotsKill: exception in PostImpactStickFix (code=0x{:X})",
							static_cast<unsigned>(GetExceptionCode()));
						lastLogTick2 = now;
					}
				}

				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct HandleProjectileAttackHook
		{
			static void* thunk(RE::Character* a_character, RE::HitData* a_hitData)
			{
				__try {
					HeadshotLogic::EvaluateHitData(a_character, a_hitData);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					logger::error("HeadshotsKill: exception in EvaluateHitData (code=0x{:X})",
						static_cast<unsigned>(GetExceptionCode()));
				}
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
	if (!a_actor || a_actor->IsDeleted() || !a_actor->Is3DLoaded()) return false;
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
		const auto firstByte = *reinterpret_cast<std::uint8_t*>(hook.address());
		const auto firstWord = *reinterpret_cast<std::uint16_t*>(hook.address());

		if (firstWord == 0x90FF) {
			// Original 6-byte indirect call: NOP-fill then write our 5-byte call
			REL::safe_fill(hook.address(), 0x90, 6);
			WriteThunkCall5<ProjectileImpactHook>(hook.address());
		} else if (firstByte == 0xE8) {
			// Another mod already placed a 5-byte relative call here; chain through it
			WriteThunkCall5<ProjectileImpactHook>(hook.address());
		} else {
			logger::critical("HeadshotsKill: ProjectileImpact site has unexpected bytes (0x{:02X}{:02X})",
				firstByte, *reinterpret_cast<std::uint8_t*>(hook.address() + 1));
			logger::critical("HeadshotsKill: hook NOT installed - headshot detection disabled");
			return;
		}
	}

	{
		auto hook = Reloc::Address(RelocId::HandleProjectileAttack);
		const auto firstByte = *reinterpret_cast<std::uint8_t*>(hook.address());
		if (firstByte == 0xE8) {
			WriteThunkCall5<HandleProjectileAttackHook>(hook.address());
		} else {
			logger::critical("HeadshotsKill: HandleProjectileAttack site has unexpected byte (0x{:02X})",
				firstByte);
			logger::critical("HeadshotsKill: hit-data hook NOT installed - OHKO disabled");
			return;
		}
	}

	logger::info("HeadshotsKill: hooks installed");
}

void Hooks::StartPeriodicTrackerUpdate()
{
	static std::atomic<bool> running{ false };
	if (running.exchange(true)) return;

	std::thread([]() {
		auto lastFullUpdate = std::chrono::steady_clock::now();
		while (true) {
			auto* settings = Settings::GetSingleton();
			const bool blinking = settings->enableHelmetHighlight && settings->enableHighlightBlink;
			const auto interval = blinking ? std::chrono::milliseconds(50) : std::chrono::milliseconds(500);
			std::this_thread::sleep_for(interval);

			auto* tracker = PlayerHelmetTracker::GetSingleton();
			if (!tracker->IsTracking()) {
				running.store(false);
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			const bool doFullUpdate = (now - lastFullUpdate) >= std::chrono::milliseconds(500);
			if (doFullUpdate) lastFullUpdate = now;

			SKSE::GetTaskInterface()->AddTask([doFullUpdate]() {
				auto* tracker = PlayerHelmetTracker::GetSingleton();
				if (!tracker->IsTracking()) return;
				if (doFullUpdate) {
					tracker->Update();
				} else {
					tracker->UpdateHighlightOnly();
				}
			});
		}
	}).detach();
}
