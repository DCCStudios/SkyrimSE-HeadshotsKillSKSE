#pragma once

#include "PCH.h"

class Settings;

namespace HeadshotLogic
{
	/// ProjectileImpact hook — runs all pre-filters (weapon type, race, level, etc.)
	/// and pushes a candidate token if they all pass.
	void OnProjectileImpact(RE::Projectile* a_projectile);

	/// Post-impact fixup: runs AFTER the original handler (and Ricochet/CIF) to
	/// override impactResult back to kStick for bare-headed headshots.
	void PostImpactStickFix(RE::Projectile* a_projectile);

	/// HandleProjectileAttack hook — checks damageLimb == kHead (engine body part)
	/// and applies OHKO (+ deferred helmet knockoff for humanoids) when a candidate exists.
	void EvaluateHitData(RE::Character* a_character, RE::HitData* a_hitData);

	/// Returns true if the actor has head armor with armor rating > 0.
	bool HasProtectiveHeadArmor(RE::Actor* a_actor);

	/// Simulates a player helmet knockoff (for debug menu). Calls DeferKnockHelmetOff + HP reduction.
	void SimulatePlayerHelmetKnockoff(RE::Actor* a_player, Settings* a_settings);

	/// TESHitEvent sink for melee helmet knockoff (fallback when Precision is not installed).
	class MeleeHitHandler : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		static MeleeHitHandler* GetSingleton();
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event,
			RE::BSTEventSource<RE::TESHitEvent>* a_source) override;
	};

	void RegisterMeleeHitSink();

	/// Attempt to connect to Precision API for accurate melee head detection.
	/// Returns true if Precision was found and callback registered.
	bool TryRegisterPrecision(SKSE::PluginHandle a_pluginHandle);

	/// Returns true if Precision integration is active (head-based melee detection).
	bool IsPrecisionActive();

	/// Computes effective helmet knockoff chance after weight penalty and skill bonus.
	float ComputeEffectiveKnockoffChance(float baseChance, RE::TESObjectARMO* helmet,
		RE::Actor* attacker, bool isMelee, bool is1H, Settings* s, bool isPlayer);
}
