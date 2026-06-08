#include "PlayerHelmetTracker.h"
#include "HeadshotSound.h"
#include "Hooks.h"
#include "Settings.h"

#include <cmath>

void PlayerHelmetTracker::OnHelmetKnockedOff(RE::ObjectRefHandle a_droppedRef, RE::FormID a_armorFormID)
{
	if (!a_droppedRef || !a_armorFormID) return;

	auto ref = a_droppedRef.get();
	{
		std::lock_guard lock(stateMutex);
		trackedHelmetRef   = a_droppedRef;
		trackedArmorFormID = a_armorFormID;
		trackedRefFormID   = ref ? ref->GetFormID() : 0;
		trackingStart      = std::chrono::steady_clock::now();
		isTracking         = true;
		markerActive       = false;
	}

	auto* settings = Settings::GetSingleton();

	RE::DebugNotification("Your helmet was knocked off!");
	PlayPlayerHelmetKnockoffSound(a_armorFormID);

	if (settings->enableHelmetMapMarker) {
		PlaceHelmetMarker();
	}

	Hooks::StartPeriodicTrackerUpdate();

	if (settings->enableDebugLogging) {
		logger::info("PlayerHelmetTracker: started tracking armor {:08X}", a_armorFormID);
	}
}

void PlayerHelmetTracker::Update()
{
	if (!isTracking) return;

	auto* settings = Settings::GetSingleton();
	const auto now = std::chrono::steady_clock::now();

	// Check timeout
	const float trackMinutes = settings->helmetTrackingDurationMinutes;
	const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - trackingStart).count();
	if (elapsed >= static_cast<long long>(trackMinutes * 60.0f)) {
		if (settings->enableDebugLogging) {
			logger::info("PlayerHelmetTracker: tracking timed out after {:.1f} min", trackMinutes);
		}
		StopTracking();
		return;
	}

	// Check if the dropped ref is still valid and in the world
	auto ref = trackedHelmetRef.get();
	if (!ref) {
		StopTracking();
		return;
	}

	// If the player equipped another helmet, stop tracking
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		auto* changes = player->GetInventoryChanges();
		if (changes && changes->entryList) {
			for (auto* entry : *changes->entryList) {
				if (!entry || !entry->object || !entry->IsWorn()) continue;
				auto* ar = entry->object->As<RE::TESObjectARMO>();
				if (!ar) continue;
				if (ar->GetFormID() == trackedArmorFormID) continue;
				if (ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHead) ||
				    ar->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kHair)) {
					auto armorType = ar->GetArmorType();
					bool isRealArmor = (armorType == RE::BGSBipedObjectForm::ArmorType::kLightArmor ||
					                    armorType == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor);
					if (isRealArmor && ar->GetArmorRating() > 0) {
						StopTracking();
						return;
					}
				}
			}
		}
	}

	// Apply highlight (Update is called ~every 500ms from the background thread)
	if (settings->enableHelmetHighlight) {
		ApplyHighlight();
	}

	// Update map marker position in case the helmet moved (rolled, fell, etc.)
	if (settings->enableHelmetMapMarker && markerActive) {
		PlaceHelmetMarker();
	}
}

bool PlayerHelmetTracker::IsInCooldown() const
{
	std::lock_guard lock(stateMutex);
	if (!cooldownActive) return false;
	auto* settings = Settings::GetSingleton();
	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cooldownStart).count();
	return elapsed < static_cast<long long>(settings->playerCooldownSeconds * 1000.0f);
}

void PlayerHelmetTracker::StartCooldown()
{
	std::lock_guard lock(stateMutex);
	cooldownStart = std::chrono::steady_clock::now();
	cooldownActive = true;
	if (Settings::GetSingleton()->enableDebugLogging) {
		logger::info("PlayerHelmetTracker: cooldown started ({:.0f}s window)",
			Settings::GetSingleton()->playerCooldownSeconds);
	}
}

void PlayerHelmetTracker::StopTracking()
{
	std::lock_guard lock(stateMutex);
	if (!isTracking) return;

	if (markerActive) {
		RemoveHelmetMarker();
	}

	// Clear highlight on the ref
	auto ref = trackedHelmetRef.get();
	if (ref && ref->Is3DLoaded()) {
		RE::NiColorA clear{ 0.0f, 0.0f, 0.0f, 0.0f };
		if (auto* obj3D = ref->Get3D()) {
			obj3D->TintScenegraph(clear);
		}
	}

	isTracking = false;
	trackedHelmetRef = RE::ObjectRefHandle{};
	trackedArmorFormID = 0;
	markerActive = false;

	if (Settings::GetSingleton()->enableDebugLogging) {
		logger::info("PlayerHelmetTracker: stopped tracking");
	}
}

void PlayerHelmetTracker::ReturnHelmetToPlayer()
{
	if (!trackedArmorFormID) return;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return;

	auto* armorForm = RE::TESForm::LookupByID<RE::TESObjectARMO>(trackedArmorFormID);
	if (!armorForm) return;

	// Delete the world reference if it still exists
	auto ref = trackedHelmetRef.get();
	if (ref) {
		ref->Disable();
		ref->SetDelete(true);
	}

	player->AddObjectToContainer(armorForm, nullptr, 1, nullptr);
	RE::ActorEquipManager::GetSingleton()->EquipObject(player, armorForm);

	if (Settings::GetSingleton()->enableDebugLogging) {
		logger::info("PlayerHelmetTracker: returned armor {:08X} to player", trackedArmorFormID);
	}
}

void PlayerHelmetTracker::Reset()
{
	std::lock_guard lock(stateMutex);
	isTracking = false;
	trackedHelmetRef = RE::ObjectRefHandle{};
	trackedArmorFormID = 0;
	trackedRefFormID = 0;
	markerActive = false;
	cooldownActive = false;
}

void PlayerHelmetTracker::PlaceHelmetMarker()
{
	auto ref = trackedHelmetRef.get();
	if (!ref) return;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return;

	auto* tes = RE::TES::GetSingleton();
	if (!tes) return;

	const RE::TESForm* worldOrCell = nullptr;
	if (tes->interiorCell) {
		worldOrCell = tes->interiorCell;
	} else {
		worldOrCell = player->GetWorldspace();
	}
	if (!worldOrCell) return;

	using SetPlayerMapMarker_t = void(*)(RE::PlayerCharacter*, const RE::NiPoint3&, const RE::TESForm*);
	static REL::Relocation<SetPlayerMapMarker_t> SetPlayerMapMarker{ RELOCATION_ID(39458, 40535) };

	if (!markerActive) {
		RE::DebugNotification("Your map marker was moved to track your helmet.");
	}

	RE::NiPoint3 pos = ref->GetPosition();
	SetPlayerMapMarker(player, pos, worldOrCell);
	markerActive = true;

	if (Settings::GetSingleton()->enableDebugLogging) {
		logger::info("PlayerHelmetTracker: placed map marker at ({:.0f}, {:.0f}, {:.0f})",
			pos.x, pos.y, pos.z);
	}
}

void PlayerHelmetTracker::RemoveHelmetMarker()
{
	if (!markerActive) return;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return;

	using RemovePlayerMapMarker_t = void(*)(RE::PlayerCharacter*);
	static REL::Relocation<RemovePlayerMapMarker_t> RemovePlayerMapMarker{ RELOCATION_ID(39459, 40536) };

	RemovePlayerMapMarker(player);
	markerActive = false;

	if (Settings::GetSingleton()->enableDebugLogging) {
		logger::info("PlayerHelmetTracker: removed helmet map marker");
	}
}

void PlayerHelmetTracker::UpdateHighlightOnly()
{
	if (!isTracking) return;
	auto* settings = Settings::GetSingleton();
	if (settings->enableHelmetHighlight) {
		ApplyHighlight();
	}
}

void PlayerHelmetTracker::ApplyHighlight()
{
	auto ref = trackedHelmetRef.get();
	if (!ref || !ref->Is3DLoaded()) return;

	auto* obj3D = ref->Get3D();
	if (!obj3D) return;

	auto* settings = Settings::GetSingleton();
	float alpha = settings->highlightAlpha;

	if (settings->enableHighlightBlink) {
		const auto now = std::chrono::steady_clock::now();
		const double elapsed = std::chrono::duration<double>(now - trackingStart).count();
		const double phase = std::sin(elapsed * settings->highlightBlinkFrequency * 6.2831853);
		alpha *= static_cast<float>(phase * 0.5 + 0.5);
	}

	RE::NiColorA color{ settings->highlightR, settings->highlightG, settings->highlightB, alpha };
	obj3D->TintScenegraph(color);
}

// =============================================================================
// SKSE Co-save
// =============================================================================

void PlayerHelmetTracker::OnSave(SKSE::SerializationInterface* a_intfc)
{
	auto* tracker = GetSingleton();
	if (!a_intfc->OpenRecord(kCoSaveType, kCoSaveVersion)) return;

	const std::uint8_t tracking = tracker->isTracking ? 1 : 0;
	a_intfc->WriteRecordData(&tracking, sizeof(tracking));

	if (tracker->isTracking) {
		a_intfc->WriteRecordData(&tracker->trackedArmorFormID, sizeof(tracker->trackedArmorFormID));
		a_intfc->WriteRecordData(&tracker->trackedRefFormID, sizeof(tracker->trackedRefFormID));
	}
}

void PlayerHelmetTracker::OnLoad(SKSE::SerializationInterface* a_intfc)
{
	auto* tracker = GetSingleton();
	tracker->Reset();

	std::uint32_t type, version, length;
	while (a_intfc->GetNextRecordInfo(type, version, length)) {
		if (type != kCoSaveType) continue;

		std::uint8_t tracking = 0;
		a_intfc->ReadRecordData(&tracking, sizeof(tracking));

		if (tracking && version >= 1) {
			RE::FormID savedArmorID = 0;
			a_intfc->ReadRecordData(&savedArmorID, sizeof(savedArmorID));

			RE::FormID savedRefID = 0;
			if (version >= 2) {
				a_intfc->ReadRecordData(&savedRefID, sizeof(savedRefID));
			}

			RE::FormID resolvedArmorID = 0;
			RE::FormID resolvedRefID = 0;
			a_intfc->ResolveFormID(savedArmorID, resolvedArmorID);
			if (savedRefID) {
				a_intfc->ResolveFormID(savedRefID, resolvedRefID);
			}

			auto* settings = Settings::GetSingleton();
			if (settings->returnHelmetOnLoad && resolvedArmorID) {
				SKSE::GetTaskInterface()->AddTask([resolvedArmorID, resolvedRefID]() {
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (!player) return;

					// Prefer picking up the original world reference so enchantments,
					// tempering, and custom names are preserved intact.
					bool recovered = false;
					if (resolvedRefID) {
						auto* refForm = RE::TESForm::LookupByID(resolvedRefID);
						if (refForm) {
							auto* ref = refForm->As<RE::TESObjectREFR>();
							if (ref && !ref->IsDeleted()) {
								player->PickUpObject(ref, 1, false, false);
								recovered = true;
							}
						}
					}

					// Fallback: if the world ref is gone, add a base-form copy
					if (!recovered) {
						auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(resolvedArmorID);
						if (armor) {
							player->AddObjectToContainer(armor, nullptr, 1, nullptr);
						}
					}

					// Equip the recovered helmet
					auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(resolvedArmorID);
					if (armor) {
						RE::ActorEquipManager::GetSingleton()->EquipObject(player, armor);
					}

					// Remove the helmet marker that persisted in the save
					using RemovePlayerMapMarker_t = void(*)(RE::PlayerCharacter*);
					static REL::Relocation<RemovePlayerMapMarker_t> RemovePlayerMapMarker{
						RELOCATION_ID(39459, 40536) };
					RemovePlayerMapMarker(player);

					GetSingleton()->Reset();
					if (Settings::GetSingleton()->enableDebugLogging) {
						logger::info("PlayerHelmetTracker: returned armor {:08X} on load (ref {:08X}, pickup={}), removed marker",
							resolvedArmorID, resolvedRefID, recovered);
					}
				});
			}
		}
	}
}

void PlayerHelmetTracker::OnRevert(SKSE::SerializationInterface*)
{
	GetSingleton()->Reset();
}

// =============================================================================
// Container changed event (auto-equip on pickup)
// =============================================================================

RE::BSEventNotifyControl PlayerHelmetTracker::ContainerChangedHandler::ProcessEvent(
	const RE::TESContainerChangedEvent* a_event,
	RE::BSTEventSource<RE::TESContainerChangedEvent>*)
{
	if (!a_event) return RE::BSEventNotifyControl::kContinue;

	auto* tracker = PlayerHelmetTracker::GetSingleton();
	if (!tracker->IsTracking()) return RE::BSEventNotifyControl::kContinue;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return RE::BSEventNotifyControl::kContinue;

	// Check if the tracked armor was picked up by the player.
	// Validate both the base object AND that the source is the world (oldContainer == 0)
	// to avoid false triggers from vendors/containers with the same armor type.
	if (a_event->baseObj == tracker->trackedArmorFormID &&
	    a_event->newContainer == player->GetFormID() &&
	    a_event->oldContainer == 0) {
		const RE::FormID armorID = tracker->trackedArmorFormID;
		SKSE::GetTaskInterface()->AddTask([armorID]() {
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorID);
			if (p && armor) {
				RE::ActorEquipManager::GetSingleton()->EquipObject(p, armor);
			}
			PlayerHelmetTracker::GetSingleton()->StopTracking();
		});

		RE::DebugNotification("Helmet recovered");
	}

	return RE::BSEventNotifyControl::kContinue;
}

void PlayerHelmetTracker::RegisterEventSink()
{
	auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
	if (holder) {
		holder->AddEventSink(ContainerChangedHandler::GetSingleton());
	}
}
