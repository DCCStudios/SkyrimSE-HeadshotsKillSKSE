#include "HeadshotLogic.h"
#include "Hooks.h"
#include "Menu.h"
#include "PlayerHelmetTracker.h"
#include "Settings.h"
#include "DismemberingFrameworkAPI.h"
#include "render/DrawHandler.h"
#include "render/D3DContext.h"

namespace Plugin
{
	inline constexpr auto NAME = "HeadshotsKill"sv;
}

namespace
{
	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			stl::report_and_fail("Failed to find standard logging directory"sv);
		}
		*path /= (std::string{ Plugin::NAME } + ".log");
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::debug);
		log->flush_on(spdlog::level::debug);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v"s);
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		Settings::GetSingleton()->Load();
		Settings::GetSingleton()->LoadSpellAllowlistJSON();
		Settings::GetSingleton()->LoadRaceConfigJSON();
		Settings::GetSingleton()->LoadUserRaceConfig();
		Settings::GetSingleton()->ResolveFormIDs();
		Hooks::Install();
		Menu::Register();
		PlayerHelmetTracker::GetSingleton()->RegisterEventSink();
		if (!HeadshotLogic::TryRegisterPrecision(SKSE::GetPluginHandle())) {
			HeadshotLogic::RegisterMeleeHitSink();
		}
		if (DismemberingFrameworkAPI::LoadAPI()) {
			logger::info("Dismembering Framework API loaded (v{})", DismemberingFrameworkAPI::g_API->GetVersion());
		} else {
			logger::info("Dismembering Framework not detected (dismember-on-OHKO disabled)");
		}
		logger::info("{} initialized", Plugin::NAME);
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		Settings::GetSingleton()->Load();
		Settings::GetSingleton()->LoadSpellAllowlistJSON();
		Settings::GetSingleton()->LoadRaceConfigJSON();
		Settings::GetSingleton()->LoadUserRaceConfig();
		Settings::GetSingleton()->ResolveFormIDs();
		Hooks::ClearBareHeads();
		if (Settings::GetSingleton()->enableDebugHitZones) {
			Render::InstallHooks();
			if (Render::HasContext()) {
				DrawHandler::GetSingleton()->Initialize();
			}
		}
		break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("{} loading", Plugin::NAME);

	SKSE::Init(a_skse);
	// Two write_call<5> hooks (projectile impact + handle projectile attack) need extra trampoline space.
	SKSE::AllocTrampoline(128);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener(MessageHandler)) {
		logger::error("Failed to register messaging listener");
		return false;
	}

	auto* serial = SKSE::GetSerializationInterface();
	serial->SetUniqueID('HSKL');
	serial->SetSaveCallback(PlayerHelmetTracker::OnSave);
	serial->SetLoadCallback(PlayerHelmetTracker::OnLoad);
	serial->SetRevertCallback(PlayerHelmetTracker::OnRevert);

	return true;
}
