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
}
