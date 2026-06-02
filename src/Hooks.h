#pragma once

#include "PCH.h"

namespace Hooks
{
	void Install();

	/// Push a candidate token after all pre-filters pass in ProjectileImpact.
	/// a_headByBone: true if bone-name / geometric fallback already confirmed a head hit.
	void PushPendingCandidate(std::uint32_t a_aggressorFormID, std::uint32_t a_targetFormID,
		std::uint8_t a_category, bool a_headByBone);

	/// Consume a matching candidate (called from HandleProjectileAttack).
	/// Returns false if no matching candidate exists or it has expired.
	/// a_outHeadByBone: receives whether the bone-name fallback confirmed a head hit.
	bool ConsumePendingCandidate(std::uint32_t a_aggressorFormID, std::uint32_t a_targetFormID,
		std::uint8_t& a_outCategory, bool& a_outHeadByBone);

	/// Register an actor whose head is now bare (helmet knocked off by us).
	/// OnProjectileImpact will patch damageRootNode to the flesh head bone for
	/// subsequent head-area hits, so Ricochet / CIF see skin, not metal.
	void RegisterBareHead(RE::FormID a_actorFormID);

	/// True if the actor was registered as bare-headed AND currently wears no
	/// heavy/light head armor (auto-clears once they re-equip one).
	bool IsBareHead(RE::Actor* a_actor);

	/// Clear all bare-head registrations (call on game load / new game).
	void ClearBareHeads();

	/// Starts the periodic PlayerHelmetTracker update (self-rescheduling task).
	void StartPeriodicTrackerUpdate();
}
