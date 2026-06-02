#pragma once

#include "PCH.h"

class PlayerHelmetTracker
{
public:
	static PlayerHelmetTracker* GetSingleton()
	{
		static PlayerHelmetTracker instance;
		return &instance;
	}

	void OnHelmetKnockedOff(RE::ObjectRefHandle a_droppedRef, RE::FormID a_armorFormID);
	void Update();
	void StopTracking();
	void ReturnHelmetToPlayer();
	void Reset();

	bool IsTracking() const { return isTracking; }
	bool IsInCooldown() const;
	void StartCooldown();

	// SKSE co-save
	static void OnSave(SKSE::SerializationInterface* a_intfc);
	static void OnLoad(SKSE::SerializationInterface* a_intfc);
	static void OnRevert(SKSE::SerializationInterface* a_intfc);

	// Container changed event for auto-equip on pickup
	class ContainerChangedHandler : public RE::BSTEventSink<RE::TESContainerChangedEvent>
	{
	public:
		static ContainerChangedHandler* GetSingleton()
		{
			static ContainerChangedHandler instance;
			return &instance;
		}
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESContainerChangedEvent* a_event,
			RE::BSTEventSource<RE::TESContainerChangedEvent>* a_source) override;
	};

	void RegisterEventSink();

private:
	PlayerHelmetTracker() = default;

	void PlaceHelmetMarker();
	void RemoveHelmetMarker();
	void ApplyHighlight();

	RE::ObjectRefHandle trackedHelmetRef{};
	RE::FormID          trackedArmorFormID{ 0 };
	RE::FormID          trackedRefFormID{ 0 };

	std::chrono::steady_clock::time_point trackingStart{};

	// Cooldown: independent of tracking. Starts on any bare-head hit.
	std::chrono::steady_clock::time_point cooldownStart{};
	bool cooldownActive{ false };

	bool isTracking{ false };
	bool markerActive{ false };

	static constexpr std::uint32_t kCoSaveType = 'PHKT';
	static constexpr std::uint32_t kCoSaveVersion = 2;
};
