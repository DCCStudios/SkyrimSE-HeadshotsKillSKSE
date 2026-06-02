#include "HeadshotLogic.h"
#include "HeadshotSound.h"
#include "Hooks.h"
#include "PlayerHelmetTracker.h"
#include "Settings.h"

#include "RE/B/BGSBodyPartData.h"
#include "RE/B/BGSBodyPartDefs.h"
#include "RE/B/bhkRigidBody.h"
#include "RE/H/hkpRigidBody.h"
#include "RE/H/hkVector4.h"
#include "RE/T/TESRace.h"

#include <cstring>

namespace HeadshotLogic
{
	namespace
	{
		bool IsFollower(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsPlayerRef()) {
				return false;
			}
			if (a_actor->IsPlayerTeammate()) {
				return true;
			}
			if (a_actor->IsCommandedActor()) {
				auto commanding = a_actor->GetCommandingActor();
				if (commanding) {
					RE::Actor* commander = commanding.get();
					if (commander && (commander->IsPlayerRef() || commander->IsPlayerTeammate())) {
						return true;
					}
				}
			}
			return false;
		}

		bool RaceEdidEquals(const char* a_race, const char* a_test)
		{
			return a_race && a_test && _stricmp(a_race, a_test) == 0;
		}

		bool IsSmallAnimalRace(const char* edid)
		{
			static const char* k[] = {
				"WolfRace", "DLC1BlackWolfRace", "SabreCatRace", "SabreCatSnowRace",
				"FoxRace", "SnowFoxRace", "DeerRace", "ElkRace", "DLC1DeerRace",
				"RabbitRace", "HareRace", "GoatRace", "GoatDomesticRace",
				"DogRace", "DogCompanionRace", "DogLabradorRace", "DogHuskyRace",
				"SkeeverRace", "HorkerRace", "ChickenRace", nullptr
			};
			for (auto p = k; *p; ++p) {
				if (RaceEdidEquals(edid, *p)) {
					return true;
				}
			}
			return false;
		}

		bool IsGiantRace(const char* edid) { return RaceEdidEquals(edid, "GiantRace"); }
		bool IsTrollRace(const char* edid)
		{
			return RaceEdidEquals(edid, "TrollRace") || RaceEdidEquals(edid, "FrostTrollRace");
		}
		bool IsBearRace(const char* edid)
		{
			return RaceEdidEquals(edid, "BearRace") || RaceEdidEquals(edid, "BearSnowRace") ||
			       RaceEdidEquals(edid, "CaveBearRace") || RaceEdidEquals(edid, "DLC2SnowBearRace");
		}
		bool IsMammothRace(const char* edid) { return RaceEdidEquals(edid, "MammothRace"); }

		/// Dwemer constructs (spheres, spiders, centurions). Never headshot-OHKO.
		bool IsDwemerAutomatonRace(const char* edid)
		{
			static const char* k[] = {
				"DwarvenSpiderRace", "DLC1DwarvenSpiderRace", "DwarvenSphereRace",
				"SteamCenturionRace", "DLC2SteamCenturionRace",
				nullptr
			};
			for (auto p = k; *p; ++p) {
				if (RaceEdidEquals(edid, *p)) {
					return true;
				}
			}
			return false;
		}

		bool EdidContainsCI(const char* a_edid, const char* a_needle)
		{
			if (!a_edid || !a_needle || !a_needle[0]) {
				return false;
			}
			const std::size_t needleLen = std::strlen(a_needle);
			const std::size_t edidLen = std::strlen(a_edid);
			if (needleLen > edidLen) {
				return false;
			}
			for (std::size_t i = 0; i + needleLen <= edidLen; ++i) {
				if (_strnicmp(a_edid + i, a_needle, needleLen) == 0) {
					return true;
				}
			}
			return false;
		}

		bool HasAnyKeyword(RE::Actor* a_target, std::initializer_list<std::string_view> a_keywords)
		{
			for (auto kw : a_keywords) {
				if (a_target->HasKeywordString(kw)) {
					return true;
				}
			}
			return false;
		}

		bool IsFalmerRace(const char* edid)
		{
			return RaceEdidEquals(edid, "FalmerRace") || RaceEdidEquals(edid, "defaultFalmerRace");
		}

		bool IsFrostbiteSpiderRace(const char* edid)
		{
			return RaceEdidEquals(edid, "FrostbiteSpiderRace");
		}

		bool IsChaurusRace(const char* edid)
		{
			return RaceEdidEquals(edid, "ChaurusRace") || RaceEdidEquals(edid, "ChaurusReaperRace");
		}

		enum class Category : std::uint8_t
		{
			None,
			Humanoid,
			SmallAnimal,
			Giant,
			Troll,
			Bear,
			Mammoth,
			SpiderGiant,
			Chaurus
		};

		Category ClassifyActor(RE::Actor* a_target)
		{
			auto*       race = a_target->GetRace();
			const char* rid  = race ? race->GetFormEditorID() : nullptr;

			// Hard excludes
			if (HasAnyKeyword(a_target, { "ActorTypeSpriggan"sv }) ||
			    (rid && EdidContainsCI(rid, "Spriggan"))) {
				return Category::None;
			}

			if (rid && IsDwemerAutomatonRace(rid)) {
				return Category::None;
			}
			if (HasAnyKeyword(a_target, {
				    "ActorTypeDwemer"sv,
				    "ActorTypeDwarven"sv,
				    "ActorTypeAutomaton"sv
				}) || (rid && (EdidContainsCI(rid, "Dwarven") || EdidContainsCI(rid, "Centurion") ||
				       EdidContainsCI(rid, "Ballista") || EdidContainsCI(rid, "Construct")))) {
				return Category::None;
			}

			// Humanoids (explicit Falmer include)
			if (HasAnyKeyword(a_target, { "ActorTypeFalmer"sv })) {
				return Category::Humanoid;
			}
			if (rid && IsFalmerRace(rid)) {
				return Category::Humanoid;
			}
			if (a_target->HasKeywordString("ActorTypeNPC"sv)) {
				return Category::Humanoid;
			}

			// Large creature buckets (keyword-first for mod coverage, then race fallback)
			if (HasAnyKeyword(a_target, { "ActorTypeGiant"sv }) || (rid && IsGiantRace(rid))) return Category::Giant;
			if (HasAnyKeyword(a_target, { "ActorTypeTroll"sv }) || (rid && IsTrollRace(rid))) return Category::Troll;
			if (HasAnyKeyword(a_target, { "ActorTypeBear"sv }) || (rid && IsBearRace(rid))) return Category::Bear;
			if (HasAnyKeyword(a_target, { "ActorTypeMammoth"sv }) || (rid && IsMammothRace(rid))) return Category::Mammoth;

			// Chaurus bucket
			if (HasAnyKeyword(a_target, { "ActorTypeChaurus"sv }) ||
			    (rid && (IsChaurusRace(rid) || EdidContainsCI(rid, "Chaurus")))) {
				return Category::Chaurus;
			}

			// Frostbite spiders: giant subtype first, then scaled giant fallback, else small.
			const bool frostbiteSpiderByKeyword = HasAnyKeyword(a_target, {
				"ActorTypeFrostbiteSpider"sv,
				"ActorTypeSpider"sv
			});
			const bool frostbiteSpiderByRace = rid && (IsFrostbiteSpiderRace(rid) || EdidContainsCI(rid, "FrostbiteSpider"));
			if (frostbiteSpiderByKeyword || frostbiteSpiderByRace) {
				const bool giantSubtype =
					HasAnyKeyword(a_target, { "ActorTypeGiantFrostbiteSpider"sv, "ActorTypeGiantSpider"sv }) ||
					(rid && (EdidContainsCI(rid, "GiantFrostbiteSpider") || EdidContainsCI(rid, "FrostbiteSpiderGiant")));
				if (giantSubtype) {
					return Category::SpiderGiant;
				}
				const float th = Settings::GetSingleton()->giantSpiderScaleThreshold;
				return (a_target->GetScale() >= th) ? Category::SpiderGiant : Category::SmallAnimal;
			}

			// Small animals (keyword-first)
			if (HasAnyKeyword(a_target, { "ActorTypeAnimal"sv })) return Category::SmallAnimal;
			if (rid && IsSmallAnimalRace(rid)) return Category::SmallAnimal;

			return Category::None;
		}

		bool IsRaceBlocked(RE::Actor* a_target, Settings* a_s)
		{
			auto*       race = a_target->GetRace();
			const char* rid  = race ? race->GetFormEditorID() : nullptr;
			if (!rid) {
				return true;
			}
			if (IsFalmerRace(rid)) {
				return false;
			}

			// Check JSON + user race config (user whitelist can override)
			std::string ridStr(rid);
			if (a_s->IsRaceBlockedByConfig(ridStr)) {
				return true;
			}

			// Legacy INI blocklist
			for (const auto& blocked : a_s->raceBlocklist) {
				if (!blocked.empty() && _stricmp(rid, blocked.c_str()) == 0) {
					return true;
				}
			}
			return false;
		}

		bool HasKeywordImmunity(RE::Actor* a_target, Settings* a_s)
		{
			for (const auto& kw : a_s->keywordImmuneList) {
				if (!kw.empty() && a_target->HasKeywordString(std::string_view{ kw })) {
					return true;
				}
			}
			return false;
		}

		float SampleEffectiveChance(Category a_cat, float a_archery, Settings* a_s,
			const char* a_raceEdid = nullptr)
		{
			float base  = 0.0f;
			float skillW = 0.0f;
			switch (a_cat) {
			case Category::Humanoid:    base = a_s->chanceHumanoid; break;
			case Category::SmallAnimal: base = a_s->chanceSmallAnimal; break;
			case Category::Giant:       base = a_s->chanceGiant;   skillW = a_s->skillInfluenceGiant; break;
			case Category::Troll:       base = a_s->chanceTroll;   skillW = a_s->skillInfluenceTroll; break;
			case Category::Bear:        base = a_s->chanceBear;    skillW = a_s->skillInfluenceBear; break;
			case Category::Mammoth:     base = a_s->chanceMammoth; skillW = a_s->skillInfluenceMammoth; break;
			case Category::SpiderGiant: base = a_s->chanceGiantSpider; skillW = a_s->skillInfluenceGiantSpider; break;
			case Category::Chaurus:     base = a_s->chanceChaurus;      skillW = a_s->skillInfluenceChaurus; break;
			default:                    return 0.0f;
			}

			// Per-race overrides from JSON/user config
			if (a_raceEdid && a_raceEdid[0]) {
				std::string edid(a_raceEdid);
				float raceChance = a_s->GetRaceChanceOverride(edid);
				if (raceChance >= 0.0f) base = raceChance;
				float raceSkill = a_s->GetRaceSkillWeightOverride(edid);
				if (raceSkill >= 0.0f) skillW = raceSkill;
			}

			const float arch  = std::clamp(a_archery, 0.0f, 100.0f);
			const float extra = arch * std::clamp(skillW, 0.0f, 2.0f);
			return std::clamp(base + extra, 0.0f, 100.0f);
		}

		bool RollPercent(float a_chance)
		{
			if (a_chance <= 0.0f)   return false;
			if (a_chance >= 100.0f) return true;
			const auto n = static_cast<std::uint32_t>(std::round(a_chance * 100.0f));
			const std::uint32_t r = static_cast<std::uint32_t>(rand()) % 10000u;
			return r < std::min<std::uint32_t>(10000u, n);
		}

		// -------------------------------------------------------------------------
		// Head-hit detection helpers
		// -------------------------------------------------------------------------

		/// Name of the head skeleton node according to the race's own body part data.
		/// Falls back to "NPC Head [Head]" (the vanilla default) if not found.
		const char* GetRaceHeadNodeName(RE::Actor* a_actor)
		{
			auto* race = a_actor ? a_actor->GetRace() : nullptr;
			if (race && race->bodyPartData) {
				auto* part = race->bodyPartData->parts[RE::BGSBodyPartDefs::LIMB_ENUM::kHead];
				if (part) {
					const char* n = part->nodeName.c_str();
					if (n && n[0]) {
						return n;
					}
				}
			}
			return "NPC Head [Head]";
		}

		/// Case-insensitive substring check for "head" anywhere in a node name.
		/// Used as a broad third-tier fallback.
		bool NodeNameContainsHead(const char* a_name)
		{
			if (!a_name) return false;
			for (const char* p = a_name; *p; ++p) {
				if ((_tolower(p[0]) == 'h') && (_tolower(p[1]) == 'e') &&
				    (_tolower(p[2]) == 'a') && (_tolower(p[3]) == 'd')) {
					return true;
				}
			}
			return false;
		}

		/// Is this node the head node for the given actor?
		/// Tier 1: exact match against race bodyPartData head node name.
		/// Tier 2: broad "head" substring (e.g. custom skeletons with non-standard naming).
		bool IsHeadNode(RE::Actor* a_actor, RE::NiAVObject* a_node)
		{
			if (!a_node) return false;
			const char* nodeName = a_node->name.c_str();
			if (!nodeName || !nodeName[0]) return false;
			const char* headName = GetRaceHeadNodeName(a_actor);
			if (_stricmp(nodeName, headName) == 0) return true;
			return NodeNameContainsHead(nodeName);
		}

		/// Recursively find the NiNode whose world position is closest to a_pos.
		/// Only considers nodes that either have a collision object or are the player.
		RE::NiNode* FindClosestNode(RE::NiNode* a_root, const RE::NiPoint3& a_pos,
			float& a_bestDistSq, bool a_requireCollision)
		{
			if (!a_root) return nullptr;

			RE::NiNode* best = nullptr;

			for (auto& childPtr : a_root->GetChildren()) {
				auto* av = childPtr.get();
				if (!av) continue;
				auto* node = av->AsNode();
				if (!node) continue;
				float childDist;
				auto* candidate = FindClosestNode(node, a_pos, childDist, a_requireCollision);
				if (candidate && childDist < a_bestDistSq) {
					a_bestDistSq = childDist;
					best         = candidate;
				}
			}

			if (!a_requireCollision || a_root->collisionObject) {
				const auto& tr = a_root->world.translate;
				const float dx = tr.x - a_pos.x;
				const float dy = tr.y - a_pos.y;
				const float dz = tr.z - a_pos.z;
				const float d  = dx * dx + dy * dy + dz * dz;
				if (d < a_bestDistSq) {
					a_bestDistSq = d;
					best         = a_root->AsNode();
				}
			}

			return best;
		}

		/// Determine if the projectile impact was a head hit using all available methods.
		/// a_damageRootNode: from impact->damageRootNode (may be null).
		/// a_impactPos: from impact->desiredTargetLoc (used for geometric fallback).
		bool DetectHeadHitByBone(RE::Actor* a_target, RE::NiNode* a_damageRootNode,
			const RE::NiPoint3& a_impactPos, std::string& a_outNodeName)
		{
			// Tier 1 – use damageRootNode directly if present.
			if (a_damageRootNode) {
				if (IsHeadNode(a_target, a_damageRootNode)) {
					a_outNodeName = a_damageRootNode->name.c_str() ? a_damageRootNode->name.c_str() : "(null)";
					return true;
				}
				// damageRootNode is set but isn't the head — don't do a geometric search;
				// the engine already told us which bone this was.
				return false;
			}

			// Tier 2 – no damageRootNode: find the skeleton node closest to the impact point.
			if (!a_target->Is3DLoaded() || !a_target->Get3D()) return false;
			auto* root = a_target->Get3D()->AsNode();
			if (!root) return false;

			// First pass: collision-object nodes only (more precise).
			float       bestDist = 1.0e12f;
			RE::NiNode* found    = FindClosestNode(root, a_impactPos, bestDist, true);
			if (!found || bestDist > 1.0e11f) {
				// Second pass: all nodes (skeletons with no head collision object).
				bestDist = 1.0e12f;
				found    = FindClosestNode(root, a_impactPos, bestDist, false);
			}

			if (found && IsHeadNode(a_target, found)) {
				a_outNodeName = found->name.c_str() ? found->name.c_str() : "(null)";
				return true;
			}
			return false;
		}

		// -------------------------------------------------------------------------
		// Helmet knock-off helpers
		// -------------------------------------------------------------------------

		RE::bhkRigidBody* GetRigidBody(RE::NiAVObject* a_obj)
		{
			if (!a_obj) return nullptr;
			auto* col = a_obj->GetCollisionObject();
			return col ? col->GetRigidBody() : nullptr;
		}

		void ApplyHelmetImpulse(RE::NiAVObject* a_obj, RE::NiPoint3 a_from, RE::NiPoint3 a_to,
			float a_linear, float a_angular)
		{
			RE::NiPointer<RE::bhkRigidBody> rb(GetRigidBody(a_obj));
			if (!rb || !rb->referencedObject) return;
			auto* hkp = static_cast<RE::hkpRigidBody*>(rb->referencedObject.get());
			if (!hkp || !hkp->world) return;

			RE::NiPoint3 dir = a_to - a_from;
			const float  len = dir.Unitize();
			if (len < 1e-3f) return;

			const float lm = a_linear;
			RE::hkVector4 lin(dir.x * lm, dir.y * lm, dir.z * lm, 0.0f);
			hkp->motion.ApplyLinearImpulse(lin);

			RE::hkVector4 ang(
				(static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.04f,
				(static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.04f,
				a_angular,
				0.0f);
			hkp->motion.ApplyAngularImpulse(ang);
		}

		// HasProtectiveHeadArmor moved to public API (HeadshotLogic::HasProtectiveHeadArmor)

		bool HasKnockoffableHeadArmor(RE::Actor* a_actor, Settings* a_s, RE::TESObjectARMO*& a_outArmor)
		{
			a_outArmor = nullptr;
			auto* changes = a_actor->GetInventoryChanges();
			if (!changes || !changes->entryList) return false;
			for (auto* entry : *changes->entryList) {
				if (!entry || !entry->object || !entry->IsWorn()) continue;
				auto* ar = entry->object->As<RE::TESObjectARMO>();
				if (!ar) continue;
				const bool headish =
					ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHead) ||
					ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHair) ||
					(a_s->knockoffCirclets && ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kCirclet));
				if (!headish) continue;
				if (ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kBody)) continue;
				a_outArmor = ar;
				return true;
			}
			return false;
		}

		void DeferKnockHelmetOff(RE::Actor* a_victim, RE::Actor* a_shooter, Settings* a_s, bool a_skipRoll = false)
		{
			if (!a_skipRoll && !RollPercent(a_s->helmetKnockoffChance)) return;

			RE::TESObjectARMO* armor = nullptr;
			if (!HasKnockoffableHeadArmor(a_victim, a_s, armor) || !armor) return;

			const bool isPlayer = a_victim->IsPlayerRef();
			RE::NiPointer<RE::Actor> victimPtr(a_victim);
			RE::NiPointer<RE::Actor> shooterPtr(a_shooter);
			const RE::FormID         armorId = armor->GetFormID();
			const float              linear  = isPlayer ? a_s->playerHelmetDropImpulse : a_s->helmetDropLinearImpulse;
			const float              angular = a_s->helmetDropAngularImpulse;

			// Frame N+1: RemoveItem
			SKSE::GetTaskInterface()->AddTask([victimPtr, shooterPtr, armorId, linear, angular, isPlayer]() {
				auto* victim = victimPtr.get();
				if (!victim || victim->IsDead()) return;

				auto* armorForm = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorId);
				if (!armorForm) return;

				auto* changes = victim->GetInventoryChanges();
				if (!changes || !changes->entryList) return;
				bool stillWorn = false;
				for (auto* entry : *changes->entryList) {
					if (entry && entry->object && entry->IsWorn() &&
					    entry->object->As<RE::TESObjectARMO>() == armorForm) {
						stillWorn = true;
						break;
					}
				}
				if (!stillWorn) return;

				auto* headNode = victim->GetNodeByName("NPC Head [Head]");
				if (!headNode) return;

				RE::NiPoint3 angles{};
				if (!headNode->world.rotate.ToEulerAnglesXYZ(angles)) return;

				// Spawn 15 units above the head node so the helmet reference is
				// created outside the NPC's collision capsule, never overlapping it.
				RE::NiPoint3 spawnPos = headNode->world.translate;
				spawnPos.z += 15.0f;

				auto* shooter = shooterPtr.get();
				// Impulse direction: from shooter toward the spawn point.
				const RE::NiPoint3 from = shooter ? shooter->GetPosition() : spawnPos;

				auto itemHandle = victim->RemoveItem(
					armorForm, 1, RE::ITEM_REMOVE_REASON::kDropping,
					nullptr, nullptr, &spawnPos, &angles);

				// For the player, force a 3D rebuild so the helmet mesh disappears.
				// Skip for NPCs to avoid the visual darkening artifact from a full
				// shader recompile on their meshes.
				if (isPlayer) {
					victim->Update3DModel();
				}

				// Mark this actor as bare-headed so OnProjectileImpact can patch
				// damageRootNode to the flesh head bone on subsequent head-area hits,
				// preventing Ricochet/CIF from reading the stale armor material.
				Hooks::RegisterBareHead(victim->GetFormID());

				if (isPlayer && itemHandle) {
					PlayerHelmetTracker::GetSingleton()->OnHelmetKnockedOff(itemHandle, armorId);
				}

				if (itemHandle && linear > 0.0f) {
					RE::NiPointer<RE::TESObjectREFR> droppedNi = itemHandle.get();
					if (droppedNi) {
						// Frame N+2: wait one more frame so Havok fully registers
						// the dropped reference as an independent rigid body.
						SKSE::GetTaskInterface()->AddTask([droppedNi, victimPtr, from, spawnPos, linear, angular]() {
							// Frame N+3: apply impulse now that the object is fully
							// separated from the actor's physics representation.
							SKSE::GetTaskInterface()->AddTask([droppedNi, victimPtr, from, spawnPos, linear, angular]() {
								if (!droppedNi || !droppedNi->Is3DLoaded()) return;
								auto* root = droppedNi->Get3D();
								if (!root) return;

								// Final safety: ensure the helmet has physically separated
								// from the NPC before we impulse it.  If it's still within
								// 40 units of the actor's position the body hasn't moved
								// away yet and we skip the impulse to avoid pushing the NPC.
								auto* victim = victimPtr.get();
								if (victim) {
									const RE::NiPoint3& hp = droppedNi->GetPosition();
									const RE::NiPoint3& vp = victim->GetPosition();
									const float dx = hp.x - vp.x;
									const float dy = hp.y - vp.y;
									const float dz = hp.z - vp.z;
									// 40-unit radius ≈ roughly the NPC's torso width.
									// Head is ~120+ units above the feet so the helmet
									// should always be well beyond this.
									if (dx * dx + dy * dy + dz * dz < 40.0f * 40.0f) {
										return;
									}
								}

								ApplyHelmetImpulse(root, from, spawnPos, linear, angular);
							});
						});
					}
				}
			});
		}
	}
}

// =============================================================================
// Public API
// =============================================================================

void HeadshotLogic::OnProjectileImpact(RE::Projectile* a_projectile)
{
	auto* settings = Settings::GetSingleton();
	if (!settings->enableMod) return;

	auto& rd = a_projectile->GetProjectileRuntimeData();
	if (rd.flags & (1u << 17)) return;

	const auto projFormType = a_projectile->GetFormType();
	const bool isArrow = (projFormType == RE::FormType::ProjectileArrow);
	bool isAllowedSpellProjectile = false;

	// Check if this is a spell projectile from the allowlist
	if (!isArrow && projFormType == RE::FormType::ProjectileMissile) {
		if (rd.spell) {
			if (settings->spellAllowlistResolved.count(rd.spell->GetFormID())) {
				isAllowedSpellProjectile = true;
			}
		}
	}

	if (!isArrow && !isAllowedSpellProjectile) return;
	if (rd.impacts.empty()) return;

	auto* impact = rd.impacts.front();
	if (!impact || !impact->collidee) return;

	auto targetPtr = impact->collidee.get();
	if (!targetPtr) return;

	auto* targetRef = targetPtr->AsReference();
	if (!targetRef || targetRef->IsDead() || targetRef->GetFormType() != RE::FormType::ActorCharacter) return;

	auto* target = targetRef->As<RE::Actor>();
	if (!target) return;

	// --- Diagnostic log: dump raw impact state before any patching ---
	if (settings->enableDebugLogging) {
		const char* matName = "null";
		const char* matID   = "N/A";
		std::uint32_t matFlags = 0;
		if (impact->material) {
			matName  = impact->material->materialName.c_str();
			matFlags = static_cast<std::uint32_t>(impact->material->flags.get());
		}
		const char* nodeName = impact->damageRootNode
			? impact->damageRootNode->name.c_str() : "null";
		const bool isBareHead = Hooks::IsBareHead(target);
		const bool hasProtHelm = HasProtectiveHeadArmor(target);

		logger::info("HeadshotsKill IMPACT on {:08X} | material=\"{}\" flags=0x{:X} arrowsStick={}"
			" | node=\"{}\" | impactResult={} | hasProtectiveHelmet={} | bareHeadTracked={} | power={:.3f}",
			target->GetFormID(),
			matName, matFlags,
			(matFlags & 0x2) != 0,
			nodeName,
			static_cast<int>(impact->impactResult),
			hasProtHelm,
			isBareHead,
			rd.power);
	}

	// Patch material and damageRootNode for headshots on actors without
	// protective head armor.  This ensures Ricochet Framework sees kSkin
	// (which has FLAG::kArrowsStick) instead of stale armor material, and CIF
	// resolves the correct biped slot.  Covers three cases:
	//   1) Helmet knocked off by us (g_bareHeads set)
	//   2) Actor never wore a helmet
	//   3) Actor wears a 0-rating cosmetic headpiece (hood, circlet)
	const bool hasNoProtectiveHelmet = !HasProtectiveHeadArmor(target);
	if (hasNoProtectiveHelmet) {
		const char* headName = GetRaceHeadNodeName(target);
		RE::NiNode* headNode = nullptr;
		bool nearHead = false;

		auto* hn = target->GetNodeByName(headName);
		if (hn) {
			headNode = hn->AsNode();
			const auto& hp = hn->world.translate;
			const auto& ip = impact->desiredTargetLoc;
			const float dx = hp.x - ip.x;
			const float dy = hp.y - ip.y;
			const float dz = hp.z - ip.z;
			nearHead = (dx * dx + dy * dy + dz * dz < 80.0f * 80.0f);
		}

		if (nearHead && headNode) {
			// Patch damageRootNode to flesh head bone
			if (impact->damageRootNode &&
			    _stricmp(impact->damageRootNode->name.c_str(), headName) != 0) {
				if (settings->enableDebugLogging) {
					logger::info("HeadshotsKill PATCH node: \"{}\" -> \"{}\"",
						impact->damageRootNode->name.c_str(), headName);
				}
				impact->damageRootNode = headNode;
			}

			// Patch material to skin so Ricochet Framework sees a soft surface
			// (kArrowsStick flag) instead of stale armor metal material.
			auto* skinMaterial = RE::BGSMaterialType::GetMaterialType(RE::MATERIAL_ID::kSkin);
			if (skinMaterial && impact->material != skinMaterial) {
				if (settings->enableDebugLogging) {
					logger::info("HeadshotsKill PATCH material: \"{}\" -> kSkin",
						impact->material ? impact->material->materialName.c_str() : "null");
				}
				impact->material = skinMaterial;
			}
		} else if (settings->enableDebugLogging) {
			logger::info("HeadshotsKill: no-helmet actor {:08X} but hit not near head (nearHead={}, headNode={})",
				target->GetFormID(), nearHead, headNode != nullptr);
		}
	}

	RE::Actor* shooter = nullptr;
	if (auto sh = rd.shooter.get()) {
		shooter = sh->As<RE::Actor>();
	}

	// For arrow projectiles, require bow/crossbow weapon source
	// For spell projectiles, skip weapon check (already validated via allowlist)
	if (isArrow) {
		auto* weap = rd.weaponSource;
		if (!weap) return;
		const auto wtype = weap->GetWeaponType();
		if (wtype != RE::WEAPON_TYPE::kBow && wtype != RE::WEAPON_TYPE::kCrossbow) return;

		// Require the bow to be fully drawn (power >= threshold) if enabled.
		// Crossbows are always "fully drawn" so only gate bows.
		if (settings->requireFullDraw && wtype == RE::WEAPON_TYPE::kBow) {
			if (rd.power < settings->fullDrawThreshold) {
				if (settings->enableDebugLogging) {
					logger::debug("HeadshotsKill: rejected - bow not fully drawn (power={:.3f} < {:.3f})",
						rd.power, settings->fullDrawThreshold);
				}
				return;
			}
		}
	}

	if (shooter && !shooter->CheckValidTarget(*targetRef)) return;

	const bool victimIsPlayerTeam = target->IsPlayerRef() || IsFollower(target);

	// Player helmet knockoff: fast-track to candidate push, skip NPC filters
	if (target->IsPlayerRef() && settings->enablePlayerHelmetKnockoff) {
		std::string detectedNodeName;
		const bool headByBone = DetectHeadHitByBone(target, impact->damageRootNode,
			impact->desiredTargetLoc, detectedNodeName);

		if (settings->enableDebugLogging) {
			logger::debug("HeadshotsKill: player hit - headByBone={} node={}",
				headByBone, detectedNodeName);
		}

		Hooks::PushPendingCandidate(
			shooter ? shooter->GetFormID() : 0,
			target->GetFormID(),
			static_cast<std::uint8_t>(Category::Humanoid), headByBone);
		return;
	}

	if (victimIsPlayerTeam && !settings->applyToPlayerAndFollowers) return;

	if (shooter) {
		const std::int32_t tl = static_cast<std::int32_t>(target->GetLevel());
		const std::int32_t sl = static_cast<std::int32_t>(shooter->GetLevel());
		if (tl > sl + settings->levelGapThreshold) return;
	}

	if (target->HasKeywordString("ActorTypeDragon"sv)) return;
	if (HasKeywordImmunity(target, settings) || IsRaceBlocked(target, settings)) return;

	const Category cat = ClassifyActor(target);
	if (cat == Category::None) return;

	auto* targetRace = target->GetRace();
	const char* targetRaceEdid = targetRace ? targetRace->GetFormEditorID() : nullptr;
	const float archery = shooter ? shooter->AsActorValueOwner()->GetActorValue(RE::ActorValue::kArchery) : 0.0f;
	const float chance  = SampleEffectiveChance(cat, archery, settings, targetRaceEdid);
	if (!RollPercent(chance)) return;

	if (settings->essentialMode == 0 && target->IsEssential()) return;

	// All pre-filters passed.
	// Attempt head detection via bone name + geometric fallback now, while we have
	// impact data. This result is OR-ed with damageLimb == kHead in EvaluateHitData.
	std::string detectedNodeName;
	const bool headByBone = DetectHeadHitByBone(target, impact->damageRootNode,
		impact->desiredTargetLoc, detectedNodeName);

	if (settings->enableDebugLogging) {
		logger::debug("HeadshotsKill: pre-filter passed {:08X} headByBone={} node={}",
			target->GetFormID(), headByBone, detectedNodeName);
	}

	Hooks::PushPendingCandidate(
		shooter ? shooter->GetFormID() : 0,
		target->GetFormID(),
		static_cast<std::uint8_t>(cat),
		headByBone);
}

// =============================================================================
// Post-impact fixup — runs AFTER the original handler + Ricochet/CIF hooks.
// Forces impactResult = kStick for arrows hitting bare heads so that
// Ricochet Framework's bounce decision is overridden.
// =============================================================================

void HeadshotLogic::PostImpactStickFix(RE::Projectile* a_projectile)
{
	auto* settings = Settings::GetSingleton();
	if (!settings->enableMod) return;

	auto& rd = a_projectile->GetProjectileRuntimeData();
	if (rd.impacts.empty()) return;

	const auto projFormType = a_projectile->GetFormType();
	if (projFormType != RE::FormType::ProjectileArrow &&
	    projFormType != RE::FormType::ProjectileMissile) return;

	auto* impact = rd.impacts.front();
	if (!impact || !impact->collidee) return;

	auto targetPtr = impact->collidee.get();
	if (!targetPtr) return;

	auto* targetRef = targetPtr->AsReference();
	if (!targetRef || targetRef->GetFormType() != RE::FormType::ActorCharacter) return;

	auto* target = targetRef->As<RE::Actor>();
	if (!target) return;

	if (HasProtectiveHeadArmor(target)) return;

	// Check if the hit is near the head
	const char* headName = GetRaceHeadNodeName(target);
	auto* headNode = target->GetNodeByName(headName);
	if (!headNode) return;

	const auto& hp = headNode->world.translate;
	const auto& ip = impact->desiredTargetLoc;
	const float dx = hp.x - ip.x;
	const float dy = hp.y - ip.y;
	const float dz = hp.z - ip.z;
	if (dx * dx + dy * dy + dz * dz >= 80.0f * 80.0f) return;

	// This is a headshot on an unarmored head — force stick
	if (impact->impactResult != RE::ImpactResult::kStick) {
		if (settings->enableDebugLogging) {
			logger::info("HeadshotsKill POST-FIX: forcing impactResult {} -> kStick on {:08X}",
				static_cast<int>(impact->impactResult), target->GetFormID());
		}
		impact->impactResult = RE::ImpactResult::kStick;
	}
}

void HeadshotLogic::EvaluateHitData(RE::Character* a_character, RE::HitData* a_hitData)
{
	if (!a_hitData || !a_character) return;

	auto* settings = Settings::GetSingleton();
	if (!settings->enableMod) return;

	// --- Melee helmet knockoff (separate from projectile OHKO) ---
	const bool isMeleeHit = a_hitData->flags.any(RE::HitData::Flag::kMeleeAttack);
	if (isMeleeHit) {
		const bool headByLimb = (a_hitData->damageLimb ==
			static_cast<std::uint32_t>(RE::BGSBodyPartDefs::LIMB_ENUM::kHead));
		if (headByLimb && a_hitData->weapon) {
			auto aggH = a_hitData->aggressor.get();
			auto tgtH = a_hitData->target.get();
			RE::Actor* attacker = aggH ? aggH->As<RE::Actor>() : nullptr;
			RE::Actor* victim = tgtH ? tgtH->As<RE::Actor>() : nullptr;
			if (victim && !victim->IsDead() && HasProtectiveHeadArmor(victim)) {
				const auto wtype = a_hitData->weapon->GetWeaponType();
				bool doKnockoff = false;
				if (victim->IsPlayerRef() && settings->enablePlayerMeleeHelmetKnockoff) {
					float chance = 0.0f;
					if (wtype == RE::WEAPON_TYPE::kOneHandSword || wtype == RE::WEAPON_TYPE::kOneHandAxe ||
					    wtype == RE::WEAPON_TYPE::kOneHandMace || wtype == RE::WEAPON_TYPE::kOneHandDagger) {
						chance = settings->playerMeleeKnockoffChance1H;
					} else if (wtype == RE::WEAPON_TYPE::kTwoHandSword || wtype == RE::WEAPON_TYPE::kTwoHandAxe) {
						chance = settings->playerMeleeKnockoffChance2H;
					}
					doKnockoff = RollPercent(chance);
				} else if (!victim->IsPlayerRef() && settings->enableMeleeHelmetKnockoff) {
					float chance = 0.0f;
					if (wtype == RE::WEAPON_TYPE::kOneHandSword || wtype == RE::WEAPON_TYPE::kOneHandAxe ||
					    wtype == RE::WEAPON_TYPE::kOneHandMace || wtype == RE::WEAPON_TYPE::kOneHandDagger) {
						chance = settings->meleeKnockoffChance1H;
					} else if (wtype == RE::WEAPON_TYPE::kTwoHandSword || wtype == RE::WEAPON_TYPE::kTwoHandAxe) {
						chance = settings->meleeKnockoffChance2H;
					}
					doKnockoff = RollPercent(chance);
				}
				if (doKnockoff) {
					if (settings->enableDebugLogging) {
						logger::debug("HeadshotsKill: melee helmet knockoff on {:08X}", victim->GetFormID());
					}
					DeferKnockHelmetOff(victim, attacker, settings, true);
				}
			}
		}
		return;
	}

	// Non-melee: reject non-bow/crossbow weapons (spell projectiles are allowed via allowlist)
	if (a_hitData->weapon) {
		const auto wtype = a_hitData->weapon->GetWeaponType();
		if (wtype != RE::WEAPON_TYPE::kBow && wtype != RE::WEAPON_TYPE::kCrossbow) {
			return;
		}
	}

	auto aggHandle = a_hitData->aggressor.get();
	auto tgtHandle = a_hitData->target.get();
	if (!aggHandle || !tgtHandle) return;

	RE::Actor* shooter = aggHandle->As<RE::Actor>();
	RE::Actor* target  = tgtHandle->As<RE::Actor>();
	if (!target || target->IsDead()) return;

	// CRITICAL: always consume the pending candidate FIRST, before any head check.
	// If we bail out before consuming (e.g. damageLimb != kHead) the candidate sits
	// in the queue for 2 seconds and can be consumed by any later hit — including
	// a subsequent melee — causing a spurious OHKO.
	std::uint8_t cat        = 0;
	bool         headByBone = false;
	if (!Hooks::ConsumePendingCandidate(
		    shooter ? shooter->GetFormID() : 0,
		    target->GetFormID(),
		    cat, headByBone)) {
		return;
	}

	// Head detection: engine body part (damageLimb == kHead) OR bone-name/geometric fallback.
	const bool headByLimb = (a_hitData->damageLimb ==
		static_cast<std::uint32_t>(RE::BGSBodyPartDefs::LIMB_ENUM::kHead));
	const bool isHeadHit  = headByLimb || headByBone;

	if (settings->enableDebugLogging) {
		logger::debug("HeadshotsKill: {:08X} damageLimb={} headByLimb={} headByBone={} -> isHead={}",
			target->GetFormID(), a_hitData->damageLimb, headByLimb, headByBone, isHeadHit);
	}

	if (!isHeadHit) return;

	// --- Player headshot path ---
	// Flow: helmet on -> knockoff only | no helmet + cooldown -> kill | no helmet -> HP reduction + start cooldown
	if (target->IsPlayerRef() && settings->enablePlayerHelmetKnockoff) {
		auto* tracker = PlayerHelmetTracker::GetSingleton();

		if (HasProtectiveHeadArmor(target)) {
			// Player has a helmet: attempt knockoff only (no HP reduction, no kill)
			if (!RollPercent(settings->playerHelmetKnockoffChance)) {
				if (settings->enableDebugLogging) {
					logger::debug("HeadshotsKill: player helmet survived knockoff roll ({:.1f}%)",
						settings->playerHelmetKnockoffChance);
				}
				return;
			}
			DeferKnockHelmetOff(target, shooter, settings, true);
			if (settings->enableDebugLogging) {
				logger::debug("HeadshotsKill: player helmet knocked off (no HP penalty)");
			}
		} else if (tracker->IsInCooldown() && settings->enableCooldownKill) {
			// Bare head + in cooldown window -> lethal
			a_hitData->totalDamage = settings->killDamage;
			a_hitData->physicalDamage = settings->killDamage;
			a_hitData->pushBack = 0.0f;
			if (settings->enableDebugLogging) {
				logger::debug("HeadshotsKill: player killed during cooldown window");
			}
		} else if (settings->killPlayerOnBareHeadshot && !tracker->IsInCooldown()) {
			// "Kill on bare headshot" is an instant-kill toggle for first bare hit
			a_hitData->totalDamage = settings->killDamage;
			a_hitData->physicalDamage = settings->killDamage;
			a_hitData->pushBack = 0.0f;
			if (settings->enableDebugLogging) {
				logger::debug("HeadshotsKill: player killed (bare headshot, kill enabled)");
			}
		} else {
			// Bare head, not in cooldown -> HP reduction + start cooldown
			float currentHP = target->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
			float targetHP = std::max(1.0f, currentHP * (settings->playerHealthReductionPercent / 100.0f));
			float damage = currentHP - targetHP;
			if (damage > 0.0f) {
				target->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -damage);
			}
			tracker->StartCooldown();
			if (settings->enableDebugLogging) {
				logger::debug("HeadshotsKill: player bare head hit, HP {:.0f} -> {:.0f}, cooldown started",
					currentHP, targetHP);
			}
		}
		return;
	}

	const bool isHumanoid = (static_cast<Category>(cat) == Category::Humanoid);

	if (isHumanoid && settings->enableHelmetKnockoff) {
		const bool hadProtectiveHelmet = HasProtectiveHeadArmor(target);
		if (hadProtectiveHelmet) {
			// Check bypass conditions before blocking OHKO
			bool bypassHelmet = false;

			// Perk bypass
			if (!bypassHelmet && settings->helmetBypassPerkForm && shooter) {
				if (shooter->HasPerk(settings->helmetBypassPerkForm)) {
					bypassHelmet = true;
					if (settings->enableDebugLogging) {
						logger::debug("HeadshotsKill: helmet bypass via perk {:08X}", target->GetFormID());
					}
				}
			}

			// Level-based bypass
			if (!bypassHelmet && settings->enableHelmetLevelBypass && shooter) {
				const std::int32_t sl = static_cast<std::int32_t>(shooter->GetLevel());
				const std::int32_t tl = static_cast<std::int32_t>(target->GetLevel());
				if ((sl - tl) >= settings->helmetLevelBypassThreshold) {
					bypassHelmet = true;
					if (settings->enableDebugLogging) {
						logger::debug("HeadshotsKill: helmet bypass via level gap ({} - {} = {})",
							sl, tl, sl - tl);
					}
				}
			}

			// Bow Charge Plus full-charge penetration (requires max red charge)
			if (!bypassHelmet && settings->bowChargePlusDetected && settings->enableBCPPenetration && shooter) {
				bool fullCharge = false;

				// BCP sets player.ForceAV("attackDamageMult", CS3Damage) at max charge
				// and resets it to 1.0 after ~5 seconds.  The spells are added/removed
				// instantly so HasMagicEffect won't catch them -- use the AV instead.
				if (settings->bcpCS3DamageGlobal) {
					const float atkMult = shooter->AsActorValueOwner()->GetActorValue(
						RE::ActorValue::kAttackDamageMult);
					const float cs3Val = settings->bcpCS3DamageGlobal->value;
					if (settings->enableDebugLogging) {
						logger::debug("HeadshotsKill: BCP check - attackDamageMult={:.2f} cs3Threshold={:.2f}",
							atkMult, cs3Val);
					}
					if (cs3Val > 1.0f && atkMult >= cs3Val - 0.01f) {
						fullCharge = true;
					}
				}

				if (fullCharge) {
					const float archery = shooter->AsActorValueOwner()->GetActorValue(RE::ActorValue::kArchery);
					const float skillBonus = std::max(0.0f, archery - settings->bcpSkillThreshold) * settings->bcpSkillScaleFactor;
					const float chance = std::clamp(settings->bcpBasePenetrationChance + skillBonus, 0.0f, 100.0f);
					if (RollPercent(chance)) {
						bypassHelmet = true;
						if (settings->enableDebugLogging) {
							logger::debug("HeadshotsKill: BCP penetration! archery={:.0f} chance={:.1f}%",
								archery, chance);
						}
					}
				}
			}

			if (!bypassHelmet) {
				// Helmet blocks OHKO — knock it off and do not kill.
				DeferKnockHelmetOff(target, shooter, settings);
				if (settings->enableDebugLogging) {
					logger::debug("HeadshotsKill: headshot blocked by helmet {:08X}", target->GetFormID());
				}
				return;
			}
			// Bypass successful — fall through to OHKO below
		} else if (settings->knockoffCirclets) {
			// No protective helmet: try to knock off circlets/hoods for visual flair
			DeferKnockHelmetOff(target, shooter, settings);
		}
	}

	const float kill = settings->killDamage;
	a_hitData->totalDamage    = kill;
	a_hitData->physicalDamage = kill;
	// Zero out knockback so 99999 damage doesn't launch the NPC across the room.
	a_hitData->pushBack       = 0.0f;

	if (settings->enableDebugLogging) {
		logger::debug("HeadshotsKill: OHKO {:.0f} -> {:08X} (limb={} bone={})",
			kill, target->GetFormID(), headByLimb, headByBone);
	}

	if (shooter && shooter->IsPlayerRef()) {
		PlayHeadshotKillSound();
	}
}

bool HeadshotLogic::HasProtectiveHeadArmor(RE::Actor* a_actor)
{
	if (!a_actor) return false;
	auto* changes = a_actor->GetInventoryChanges();
	if (!changes || !changes->entryList) return false;
	for (auto* entry : *changes->entryList) {
		if (!entry || !entry->object || !entry->IsWorn()) continue;
		auto* ar = entry->object->As<RE::TESObjectARMO>();
		if (!ar) continue;
		const bool coversHead =
			ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHead) ||
			ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHair);
		if (!coversHead || ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kBody)) continue;
		if (ar->GetArmorRating() > 0) return true;
	}
	return false;
}

void HeadshotLogic::SimulatePlayerHelmetKnockoff(RE::Actor* a_player, Settings* a_settings)
{
	if (!a_player || !a_settings) return;

	RE::DebugNotification("DEBUG: helmet knocked off (no HP penalty)");
	logger::info("DEBUG: simulated player helmet knockoff (knockoff only, no HP reduction)");

	// Trigger the actual knockoff via the internal function
	// We need to call through the namespace-local function. Since it's in the anon namespace,
	// we replicate the essential logic here for the simulation.
	RE::TESObjectARMO* armor = nullptr;
	auto* changes = a_player->GetInventoryChanges();
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
				armor = ar;
				break;
			}
		}
	}

	if (!armor) {
		RE::DebugNotification("DEBUG: no knockoff-able helmet found");
		return;
	}

	const RE::FormID armorId = armor->GetFormID();
	const float impulse = a_settings->playerHelmetDropImpulse;
	RE::NiPointer<RE::Actor> playerPtr(a_player);

	SKSE::GetTaskInterface()->AddTask([playerPtr, armorId, impulse]() {
		auto* victim = playerPtr.get();
		if (!victim) return;

		auto* armorForm = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorId);
		if (!armorForm) return;

		auto* headNode = victim->GetNodeByName("NPC Head [Head]");
		if (!headNode) return;

		RE::NiPoint3 angles{};
		headNode->world.rotate.ToEulerAnglesXYZ(angles);
		RE::NiPoint3 spawnPos = headNode->world.translate;
		spawnPos.z += 15.0f;

		auto itemHandle = victim->RemoveItem(
			armorForm, 1, RE::ITEM_REMOVE_REASON::kDropping,
			nullptr, nullptr, &spawnPos, &angles);

		victim->Update3DModel();
		Hooks::RegisterBareHead(victim->GetFormID());

		if (itemHandle) {
			PlayerHelmetTracker::GetSingleton()->OnHelmetKnockedOff(itemHandle, armorId);
		}
	});
}
