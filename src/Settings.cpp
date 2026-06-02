#include "Settings.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	constexpr auto kPath = L"Data/SKSE/Plugins/HeadshotsKill.ini"sv;
	constexpr auto kSpellAllowlistDir = L"Data/SKSE/Plugins/HeadshotsKill/SpellAllowlist";
	constexpr auto kRaceConfigDir = L"Data/SKSE/Plugins/HeadshotsKill/Races";
	constexpr auto kUserRaceConfigPath = L"Data/SKSE/Plugins/HeadshotsKill_UserRaces.ini";

	void TrimInPlace(std::string& s)
	{
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
			s.pop_back();
		}
		std::size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
			++i;
		}
		if (i > 0) {
			s.erase(0, i);
		}
	}

	RE::FormID ParseFormIDHex(const std::string& a_hex)
	{
		RE::FormID id = 0;
		try {
			id = static_cast<RE::FormID>(std::stoul(a_hex, nullptr, 16));
		} catch (...) {}
		return id;
	}

	std::string SimpleJsonValue(const std::string& a_json, const std::string& a_key)
	{
		auto keyPos = a_json.find("\"" + a_key + "\"");
		if (keyPos == std::string::npos) return "";
		auto colonPos = a_json.find(':', keyPos + a_key.size() + 2);
		if (colonPos == std::string::npos) return "";
		auto valStart = a_json.find_first_not_of(" \t\r\n", colonPos + 1);
		if (valStart == std::string::npos) return "";
		if (a_json[valStart] == '"') {
			auto valEnd = a_json.find('"', valStart + 1);
			if (valEnd == std::string::npos) return "";
			return a_json.substr(valStart + 1, valEnd - valStart - 1);
		}
		auto valEnd = a_json.find_first_of(",}] \t\r\n", valStart);
		return a_json.substr(valStart, valEnd - valStart);
	}
}

void Settings::ParseList(const char* a_csv, std::vector<std::string>& a_out)
{
	a_out.clear();
	if (!a_csv || !a_csv[0]) {
		return;
	}
	std::string_view sv(a_csv);
	std::string token;
	for (char c : sv) {
		if (c == ',' || c == ';' || c == '\n' || c == '\r') {
			TrimInPlace(token);
			if (!token.empty()) {
				a_out.push_back(token);
			}
			token.clear();
		} else {
			token += c;
		}
	}
	TrimInPlace(token);
	if (!token.empty()) {
		a_out.push_back(token);
	}
}

void Settings::ResetToDefaults()
{
	enableMod = true;
	enableDebugLogging = false;
	killDamage = 99999.0f;
	applyToPlayerAndFollowers = false;
	levelGapThreshold = 30;
	essentialMode = 0;
	chanceHumanoid = 100.0f;
	chanceSmallAnimal = 100.0f;
	chanceGiant = 15.0f;
	chanceTroll = 20.0f;
	chanceBear = 10.0f;
	chanceMammoth = 8.0f;
	chanceGiantSpider = 35.0f;
	chanceChaurus = 25.0f;
	giantSpiderScaleThreshold = 1.15f;
	skillInfluenceGiant = 0.35f;
	skillInfluenceTroll = 0.35f;
	skillInfluenceBear = 0.50f;
	skillInfluenceMammoth = 0.40f;
	skillInfluenceGiantSpider = 0.0f;
	skillInfluenceChaurus = 0.0f;
	enableHelmetKnockoff = true;
	helmetKnockoffChance = 30.0f;
	knockoffCirclets = false;
	helmetDropLinearImpulse = 3.0f;
	helmetDropAngularImpulse = 0.10f;
	enableMeleeHelmetKnockoff = false;
	meleeKnockoffChance1H = 10.0f;
	meleeKnockoffChance2H = 20.0f;

	requireFullDraw = true;
	fullDrawThreshold = 0.75f;

	helmetBypassPerkStr[0] = '\0';
	helmetBypassPerkForm = nullptr;
	enableHelmetLevelBypass = false;
	helmetLevelBypassThreshold = 10;

	enableBCPPenetration = true;
	bcpBasePenetrationChance = 5.0f;
	bcpSkillThreshold = 50.0f;
	bcpSkillScaleFactor = 0.5f;

	spellAllowlistBuf[0] = '\0';

	strncpy_s(headshotKillSoundFile, "headshotKillA.wav", sizeof(headshotKillSoundFile) - 1);
	headshotKillSoundFile[sizeof(headshotKillSoundFile) - 1] = '\0';
	enableHeadshotKillSound = true;
	headshotKillSoundVolume = 1.0f;

	enablePlayerHelmetKnockoff = true;
	playerHelmetKnockoffChance = 30.0f;
	playerHealthReductionPercent = 10.0f;
	killPlayerOnBareHeadshot = false;
	playerCooldownSeconds = 30.0f;
	enableCooldownKill = true;
	playerHelmetDropImpulse = 3.0f;
	enablePlayerMeleeHelmetKnockoff = false;
	playerMeleeKnockoffChance1H = 10.0f;
	playerMeleeKnockoffChance2H = 20.0f;
	returnHelmetOnLoad = true;
	helmetTrackingDurationMinutes = 5.0f;
	enableHelmetHighlight = true;
	highlightR = 1.0f;
	highlightG = 1.0f;
	highlightB = 1.0f;
	highlightAlpha = 0.6f;
	enableHelmetMapMarker = true;
	strncpy_s(playerHelmetKnockoffSoundFile, "helmetKnockoff.wav", sizeof(playerHelmetKnockoffSoundFile) - 1);
	playerHelmetKnockoffSoundFile[sizeof(playerHelmetKnockoffSoundFile) - 1] = '\0';

	const char* defRace =
		"DraugrRace,SkeletonRace,ElderRace,GhostRace,WispRace,IceWraithRace,"
		"LichRace,DremoraRace,AffinitySpriteRace,SeekerRace";
	const char* defKw = "ActorTypeUndead,ActorTypeGhost";
	strncpy_s(raceBlocklistBuf, defRace, sizeof(raceBlocklistBuf) - 1);
	raceBlocklistBuf[sizeof(raceBlocklistBuf) - 1] = '\0';
	strncpy_s(keywordImmuneBuf, defKw, sizeof(keywordImmuneBuf) - 1);
	keywordImmuneBuf[sizeof(keywordImmuneBuf) - 1] = '\0';
	ParseList(raceBlocklistBuf, raceBlocklist);
	ParseList(keywordImmuneBuf, keywordImmuneList);
}

void Settings::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	if (ini.LoadFile(kPath.data()) != SI_OK) {
		logger::warn("HeadshotsKill.ini not found; creating defaults");
		ResetToDefaults();
		Save();
		return;
	}

	enableMod = ini.GetBoolValue("General", "bEnableMod", true);
	enableDebugLogging = ini.GetBoolValue("General", "bEnableDebugLogging", false);
	killDamage = static_cast<float>(ini.GetDoubleValue("General", "fKillDamage", 99999.0));

	applyToPlayerAndFollowers = ini.GetBoolValue("Victims", "bApplyToPlayerAndFollowers", false);
	levelGapThreshold = static_cast<std::int32_t>(ini.GetLongValue("Victims", "iLevelGapThreshold", 30));
	essentialMode = static_cast<std::int32_t>(ini.GetLongValue("Victims", "iEssentialMode", 0));

	chanceHumanoid = static_cast<float>(ini.GetDoubleValue("Chances", "fHumanoid", 100.0));
	chanceSmallAnimal = static_cast<float>(ini.GetDoubleValue("Chances", "fSmallAnimal", 100.0));
	chanceGiant = static_cast<float>(ini.GetDoubleValue("Chances", "fGiant", 15.0));
	chanceTroll = static_cast<float>(ini.GetDoubleValue("Chances", "fTroll", 20.0));
	chanceBear = static_cast<float>(ini.GetDoubleValue("Chances", "fBear", 10.0));
	chanceMammoth = static_cast<float>(ini.GetDoubleValue("Chances", "fMammoth", 8.0));
	chanceGiantSpider = static_cast<float>(ini.GetDoubleValue("Chances", "fGiantSpider", 35.0));
	chanceChaurus = static_cast<float>(ini.GetDoubleValue("Chances", "fChaurus", 25.0));
	giantSpiderScaleThreshold = static_cast<float>(ini.GetDoubleValue("Chances", "fGiantSpiderScaleThreshold", 1.15));

	skillInfluenceGiant = static_cast<float>(ini.GetDoubleValue("Chances", "fSkillInfluenceGiant", 0.35));
	skillInfluenceTroll = static_cast<float>(ini.GetDoubleValue("Chances", "fSkillInfluenceTroll", 0.35));
	skillInfluenceBear = static_cast<float>(ini.GetDoubleValue("Chances", "fSkillInfluenceBear", 0.50));
	skillInfluenceMammoth = static_cast<float>(ini.GetDoubleValue("Chances", "fSkillInfluenceMammoth", 0.40));
	skillInfluenceGiantSpider = static_cast<float>(ini.GetDoubleValue("Chances", "fSkillInfluenceGiantSpider", 0.0));
	skillInfluenceChaurus = static_cast<float>(ini.GetDoubleValue("Chances", "fSkillInfluenceChaurus", 0.0));

	requireFullDraw = ini.GetBoolValue("Combat", "bRequireFullDraw", true);
	fullDrawThreshold = static_cast<float>(ini.GetDoubleValue("Combat", "fFullDrawThreshold", 0.75));

	enableHelmetKnockoff = ini.GetBoolValue("Helmet", "bEnableHelmetKnockoff", true);
	helmetKnockoffChance = static_cast<float>(ini.GetDoubleValue("Helmet", "fKnockoffChance", 30.0));
	knockoffCirclets = ini.GetBoolValue("Helmet", "bKnockoffCirclets", false);
	helmetDropLinearImpulse = static_cast<float>(ini.GetDoubleValue("Helmet", "fDropLinearImpulse", 3.0));
	helmetDropAngularImpulse = static_cast<float>(ini.GetDoubleValue("Helmet", "fDropAngularImpulse", 0.10));
	enableMeleeHelmetKnockoff = ini.GetBoolValue("Helmet", "bEnableMeleeKnockoff", false);
	meleeKnockoffChance1H = static_cast<float>(ini.GetDoubleValue("Helmet", "fMeleeKnockoffChance1H", 10.0));
	meleeKnockoffChance2H = static_cast<float>(ini.GetDoubleValue("Helmet", "fMeleeKnockoffChance2H", 20.0));

	{
		const char* perkStr = ini.GetValue("Helmet", "sHelmetBypassPerk", "");
		strncpy_s(helmetBypassPerkStr, perkStr, sizeof(helmetBypassPerkStr) - 1);
		helmetBypassPerkStr[sizeof(helmetBypassPerkStr) - 1] = '\0';
	}
	enableHelmetLevelBypass = ini.GetBoolValue("Helmet", "bEnableLevelBypass", false);
	helmetLevelBypassThreshold = static_cast<std::int32_t>(ini.GetLongValue("Helmet", "iLevelBypassThreshold", 10));

	enableBCPPenetration = ini.GetBoolValue("BowChargePlus", "bEnableBCPPenetration", true);
	bcpBasePenetrationChance = static_cast<float>(ini.GetDoubleValue("BowChargePlus", "fBasePenetrationChance", 5.0));
	bcpSkillThreshold = static_cast<float>(ini.GetDoubleValue("BowChargePlus", "fSkillThreshold", 50.0));
	bcpSkillScaleFactor = static_cast<float>(ini.GetDoubleValue("BowChargePlus", "fSkillScaleFactor", 0.5));

	{
		const char* spellList = ini.GetValue("Lists", "sSpellAllowlist", "");
		strncpy_s(spellAllowlistBuf, spellList, sizeof(spellAllowlistBuf) - 1);
		spellAllowlistBuf[sizeof(spellAllowlistBuf) - 1] = '\0';
	}

	enableHeadshotKillSound = ini.GetBoolValue("Sound", "bEnableHeadshotKillSound", true);
	{
		const char* wav = ini.GetValue("Sound", "sHeadshotKillWav", "headshotKillA.wav");
		strncpy_s(headshotKillSoundFile, wav, sizeof(headshotKillSoundFile) - 1);
		headshotKillSoundFile[sizeof(headshotKillSoundFile) - 1] = '\0';
	}
	headshotKillSoundVolume =
		static_cast<float>(ini.GetDoubleValue("Sound", "fHeadshotKillVolume", 1.0));

	enablePlayerHelmetKnockoff = ini.GetBoolValue("PlayerHelmet", "bEnable", true);
	playerHelmetKnockoffChance = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fKnockoffChance", 30.0));
	playerHealthReductionPercent = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fHealthReductionPercent", 10.0));
	killPlayerOnBareHeadshot = ini.GetBoolValue("PlayerHelmet", "bKillOnBareHeadshot", false);
	playerCooldownSeconds = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fCooldownSeconds", 30.0));
	enableCooldownKill = ini.GetBoolValue("PlayerHelmet", "bEnableCooldownKill", true);
	playerHelmetDropImpulse = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fDropImpulse", 3.0));
	enablePlayerMeleeHelmetKnockoff = ini.GetBoolValue("PlayerHelmet", "bEnableMeleeKnockoff", false);
	playerMeleeKnockoffChance1H = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fMeleeKnockoffChance1H", 10.0));
	playerMeleeKnockoffChance2H = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fMeleeKnockoffChance2H", 20.0));
	returnHelmetOnLoad = ini.GetBoolValue("PlayerHelmet", "bReturnOnLoad", true);
	helmetTrackingDurationMinutes = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fTrackingDurationMinutes", 5.0));
	enableHelmetHighlight = ini.GetBoolValue("PlayerHelmet", "bEnableHighlight", true);
	highlightR = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fHighlightR", 1.0));
	highlightG = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fHighlightG", 1.0));
	highlightB = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fHighlightB", 1.0));
	highlightAlpha = static_cast<float>(ini.GetDoubleValue("PlayerHelmet", "fHighlightAlpha", 0.6));
	enableHelmetMapMarker = ini.GetBoolValue("PlayerHelmet", "bEnableMapMarker", true);
	{
		const char* wav = ini.GetValue("PlayerHelmet", "sKnockoffSound", "");
		strncpy_s(playerHelmetKnockoffSoundFile, wav, sizeof(playerHelmetKnockoffSoundFile) - 1);
		playerHelmetKnockoffSoundFile[sizeof(playerHelmetKnockoffSoundFile) - 1] = '\0';
	}

	const char* raceList = ini.GetValue("Lists", "sRaceBlocklist",
		"DraugrRace,SkeletonRace,ElderRace,GhostRace,WispRace,IceWraithRace,NetherLichRace,"
		"LichRace,DremoraRace,AffinitySpriteRace,SeekerRace");
	const char* kwList = ini.GetValue("Lists", "sKeywordImmune",
		"ActorTypeUndead,ActorTypeGhost");

	strncpy_s(raceBlocklistBuf, raceList, sizeof(raceBlocklistBuf) - 1);
	strncpy_s(keywordImmuneBuf, kwList, sizeof(keywordImmuneBuf) - 1);
	ParseList(raceBlocklistBuf, raceBlocklist);
	ParseList(keywordImmuneBuf, keywordImmuneList);

	// clamp
	killDamage = std::max(1.0f, killDamage);
	essentialMode = std::clamp(essentialMode, 0, 1);
	auto clampPct = [](float& v) { v = std::clamp(v, 0.0f, 100.0f); };
	clampPct(chanceHumanoid);
	clampPct(chanceSmallAnimal);
	clampPct(chanceGiant);
	clampPct(chanceTroll);
	clampPct(chanceBear);
	clampPct(chanceMammoth);
	clampPct(chanceGiantSpider);
	clampPct(chanceChaurus);
	giantSpiderScaleThreshold = std::clamp(giantSpiderScaleThreshold, 1.0f, 3.0f);
	clampPct(helmetKnockoffChance);
	clampPct(meleeKnockoffChance1H);
	clampPct(meleeKnockoffChance2H);
	fullDrawThreshold = std::clamp(fullDrawThreshold, 0.5f, 1.0f);
	headshotKillSoundVolume = std::clamp(headshotKillSoundVolume, 0.0f, 1.0f);
	clampPct(playerHelmetKnockoffChance);
	clampPct(playerMeleeKnockoffChance1H);
	clampPct(playerMeleeKnockoffChance2H);
	playerHealthReductionPercent = std::clamp(playerHealthReductionPercent, 1.0f, 100.0f);
	playerCooldownSeconds = std::clamp(playerCooldownSeconds, 1.0f, 300.0f);
	playerHelmetDropImpulse = std::clamp(playerHelmetDropImpulse, 0.0f, 30.0f);
	helmetTrackingDurationMinutes = std::clamp(helmetTrackingDurationMinutes, 0.5f, 60.0f);
	highlightR = std::clamp(highlightR, 0.0f, 1.0f);
	highlightG = std::clamp(highlightG, 0.0f, 1.0f);
	highlightB = std::clamp(highlightB, 0.0f, 1.0f);
	highlightAlpha = std::clamp(highlightAlpha, 0.0f, 1.0f);
}

void Settings::Save()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	ini.SetBoolValue("General", "bEnableMod", enableMod);
	ini.SetBoolValue("General", "bEnableDebugLogging", enableDebugLogging);
	ini.SetDoubleValue("General", "fKillDamage", killDamage, nullptr, true);

	ini.SetBoolValue("Victims", "bApplyToPlayerAndFollowers", applyToPlayerAndFollowers);
	ini.SetLongValue("Victims", "iLevelGapThreshold", levelGapThreshold);
	ini.SetLongValue("Victims", "iEssentialMode", essentialMode);

	ini.SetDoubleValue("Chances", "fHumanoid", chanceHumanoid);
	ini.SetDoubleValue("Chances", "fSmallAnimal", chanceSmallAnimal);
	ini.SetDoubleValue("Chances", "fGiant", chanceGiant);
	ini.SetDoubleValue("Chances", "fTroll", chanceTroll);
	ini.SetDoubleValue("Chances", "fBear", chanceBear);
	ini.SetDoubleValue("Chances", "fMammoth", chanceMammoth);
	ini.SetDoubleValue("Chances", "fGiantSpider", chanceGiantSpider);
	ini.SetDoubleValue("Chances", "fChaurus", chanceChaurus);
	ini.SetDoubleValue("Chances", "fGiantSpiderScaleThreshold", giantSpiderScaleThreshold);
	ini.SetDoubleValue("Chances", "fSkillInfluenceGiant", skillInfluenceGiant);
	ini.SetDoubleValue("Chances", "fSkillInfluenceTroll", skillInfluenceTroll);
	ini.SetDoubleValue("Chances", "fSkillInfluenceBear", skillInfluenceBear);
	ini.SetDoubleValue("Chances", "fSkillInfluenceMammoth", skillInfluenceMammoth);
	ini.SetDoubleValue("Chances", "fSkillInfluenceGiantSpider", skillInfluenceGiantSpider);
	ini.SetDoubleValue("Chances", "fSkillInfluenceChaurus", skillInfluenceChaurus);

	ini.SetBoolValue("Combat", "bRequireFullDraw", requireFullDraw);
	ini.SetDoubleValue("Combat", "fFullDrawThreshold", fullDrawThreshold);

	ini.SetBoolValue("Helmet", "bEnableHelmetKnockoff", enableHelmetKnockoff);
	ini.SetDoubleValue("Helmet", "fKnockoffChance", helmetKnockoffChance);
	ini.SetBoolValue("Helmet", "bKnockoffCirclets", knockoffCirclets);
	ini.SetDoubleValue("Helmet", "fDropLinearImpulse", helmetDropLinearImpulse);
	ini.SetDoubleValue("Helmet", "fDropAngularImpulse", helmetDropAngularImpulse);
	ini.SetBoolValue("Helmet", "bEnableMeleeKnockoff", enableMeleeHelmetKnockoff);
	ini.SetDoubleValue("Helmet", "fMeleeKnockoffChance1H", meleeKnockoffChance1H);
	ini.SetDoubleValue("Helmet", "fMeleeKnockoffChance2H", meleeKnockoffChance2H);
	ini.SetValue("Helmet", "sHelmetBypassPerk", helmetBypassPerkStr);
	ini.SetBoolValue("Helmet", "bEnableLevelBypass", enableHelmetLevelBypass);
	ini.SetLongValue("Helmet", "iLevelBypassThreshold", helmetLevelBypassThreshold);

	ini.SetBoolValue("BowChargePlus", "bEnableBCPPenetration", enableBCPPenetration);
	ini.SetDoubleValue("BowChargePlus", "fBasePenetrationChance", bcpBasePenetrationChance);
	ini.SetDoubleValue("BowChargePlus", "fSkillThreshold", bcpSkillThreshold);
	ini.SetDoubleValue("BowChargePlus", "fSkillScaleFactor", bcpSkillScaleFactor);

	ini.SetBoolValue("Sound", "bEnableHeadshotKillSound", enableHeadshotKillSound);
	ini.SetValue("Sound", "sHeadshotKillWav", headshotKillSoundFile);
	ini.SetDoubleValue("Sound", "fHeadshotKillVolume", headshotKillSoundVolume, nullptr, true);

	ini.SetBoolValue("PlayerHelmet", "bEnable", enablePlayerHelmetKnockoff);
	ini.SetDoubleValue("PlayerHelmet", "fKnockoffChance", playerHelmetKnockoffChance);
	ini.SetDoubleValue("PlayerHelmet", "fHealthReductionPercent", playerHealthReductionPercent);
	ini.SetBoolValue("PlayerHelmet", "bKillOnBareHeadshot", killPlayerOnBareHeadshot);
	ini.SetDoubleValue("PlayerHelmet", "fCooldownSeconds", playerCooldownSeconds);
	ini.SetBoolValue("PlayerHelmet", "bEnableCooldownKill", enableCooldownKill);
	ini.SetDoubleValue("PlayerHelmet", "fDropImpulse", playerHelmetDropImpulse);
	ini.SetBoolValue("PlayerHelmet", "bEnableMeleeKnockoff", enablePlayerMeleeHelmetKnockoff);
	ini.SetDoubleValue("PlayerHelmet", "fMeleeKnockoffChance1H", playerMeleeKnockoffChance1H);
	ini.SetDoubleValue("PlayerHelmet", "fMeleeKnockoffChance2H", playerMeleeKnockoffChance2H);
	ini.SetBoolValue("PlayerHelmet", "bReturnOnLoad", returnHelmetOnLoad);
	ini.SetDoubleValue("PlayerHelmet", "fTrackingDurationMinutes", helmetTrackingDurationMinutes);
	ini.SetBoolValue("PlayerHelmet", "bEnableHighlight", enableHelmetHighlight);
	ini.SetDoubleValue("PlayerHelmet", "fHighlightR", highlightR);
	ini.SetDoubleValue("PlayerHelmet", "fHighlightG", highlightG);
	ini.SetDoubleValue("PlayerHelmet", "fHighlightB", highlightB);
	ini.SetDoubleValue("PlayerHelmet", "fHighlightAlpha", highlightAlpha);
	ini.SetBoolValue("PlayerHelmet", "bEnableMapMarker", enableHelmetMapMarker);
	ini.SetValue("PlayerHelmet", "sKnockoffSound", playerHelmetKnockoffSoundFile);

	ini.SetValue("Lists", "sRaceBlocklist", raceBlocklistBuf);
	ini.SetValue("Lists", "sKeywordImmune", keywordImmuneBuf);
	ini.SetValue("Lists", "sSpellAllowlist", spellAllowlistBuf);

	if (ini.SaveFile(kPath.data()) != SI_OK) {
		logger::error("Failed to save HeadshotsKill.ini");
	}
}

RE::TESForm* Settings::LookupFormFromString(const std::string& a_str)
{
	if (a_str.empty()) return nullptr;

	auto colonPos = a_str.find(':');
	if (colonPos == std::string::npos) return nullptr;

	std::string pluginName = a_str.substr(0, colonPos);
	std::string formIDStr = a_str.substr(colonPos + 1);
	TrimInPlace(pluginName);
	TrimInPlace(formIDStr);

	RE::FormID localID = ParseFormIDHex(formIDStr);
	if (localID == 0) return nullptr;

	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) return nullptr;

	return dh->LookupForm(localID, pluginName);
}

void Settings::ResolveFormIDs()
{
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) return;

	// Resolve helmet bypass perk
	helmetBypassPerkForm = nullptr;
	if (helmetBypassPerkStr[0]) {
		auto* form = LookupFormFromString(helmetBypassPerkStr);
		if (form) {
			helmetBypassPerkForm = form->As<RE::BGSPerk>();
			if (helmetBypassPerkForm) {
				logger::info("HeadshotsKill: resolved helmet bypass perk {:08X}", form->GetFormID());
			} else {
				logger::warn("HeadshotsKill: '{}' is not a perk", helmetBypassPerkStr);
			}
		} else {
			logger::warn("HeadshotsKill: could not resolve perk '{}'", helmetBypassPerkStr);
		}
	}

	// Detect Bow Charge Plus (check both full and light plugin lists)
	bowChargePlusDetected = false;
	bcpAttack3Effect = nullptr;
	bcpCS3DamageGlobal = nullptr;

	const char* bcpName = "Bow Charge Plus.esp";
	const RE::TESFile* bcpMod = dh->LookupLoadedModByName(bcpName);
	if (!bcpMod) {
		bcpMod = dh->LookupLoadedLightModByName(bcpName);
	}
	if (!bcpMod) {
		bcpMod = dh->LookupModByName(bcpName);
	}
	if (bcpMod) {
		bowChargePlusDetected = true;
		logger::info("HeadshotsKill: Bow Charge Plus detected ({})", bcpName);

		auto* effect = dh->LookupForm<RE::EffectSetting>(0x80B, bcpName);
		if (effect) {
			bcpAttack3Effect = effect;
			logger::info("HeadshotsKill: resolved AAABowChargeAttack3Effect {:08X}", effect->GetFormID());
		} else {
			logger::warn("HeadshotsKill: could not resolve BCP effect 0x80B");
		}

		auto* global = dh->LookupForm<RE::TESGlobal>(0x83B, bcpName);
		if (global) {
			bcpCS3DamageGlobal = global;
			logger::info("HeadshotsKill: resolved aaaCS3Damage global {:08X} value={:.2f}",
				global->GetFormID(), global->value);
		} else {
			logger::warn("HeadshotsKill: could not resolve BCP global 0x83B");
		}
	} else {
		logger::info("HeadshotsKill: Bow Charge Plus not found (checked full, light, and all mod lists)");
	}

	ResolveSpellAllowlist();
}

void Settings::ResolveSpellAllowlist()
{
	spellAllowlistResolved.clear();

	for (auto& entry : spellAllowlist) {
		entry.valid = false;
		entry.resolvedFormID = 0;

		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) continue;

		RE::FormID localID = ParseFormIDHex(entry.formIDStr);
		if (localID == 0) continue;

		auto* form = dh->LookupForm(localID, entry.pluginName);
		if (!form) {
			logger::warn("HeadshotsKill: spell allowlist entry {}:{} not found",
				entry.pluginName, entry.formIDStr);
			continue;
		}

		auto* spell = form->As<RE::SpellItem>();
		if (!spell) {
			logger::warn("HeadshotsKill: {}:{} is not a spell", entry.pluginName, entry.formIDStr);
			continue;
		}

		bool hasProjectile = false;
		for (auto* effect : spell->effects) {
			if (effect && effect->baseEffect && effect->baseEffect->data.projectileBase) {
				hasProjectile = true;
				break;
			}
		}

		if (!hasProjectile) {
			logger::warn("HeadshotsKill: spell {}:{} has no projectile effect",
				entry.pluginName, entry.formIDStr);
			continue;
		}

		entry.valid = true;
		entry.resolvedFormID = form->GetFormID();
		const char* edid = spell->GetFormEditorID();
		if (edid && edid[0]) {
			entry.resolvedName = edid;
		} else {
			const char* name = spell->GetName();
			entry.resolvedName = (name && name[0]) ? name : "";
		}
		spellAllowlistResolved.insert(form->GetFormID());
	}

	logger::info("HeadshotsKill: spell allowlist resolved {} valid entries", spellAllowlistResolved.size());
}

void Settings::LoadSpellAllowlistJSON()
{
	// Parse INI entries first
	spellAllowlist.clear();

	std::vector<std::string> iniEntries;
	ParseList(spellAllowlistBuf, iniEntries);
	for (const auto& entryStr : iniEntries) {
		auto colonPos = entryStr.find(':');
		if (colonPos == std::string::npos) continue;

		SpellAllowlistEntry entry;
		entry.pluginName = entryStr.substr(0, colonPos);
		entry.formIDStr = entryStr.substr(colonPos + 1);
		TrimInPlace(entry.pluginName);
		TrimInPlace(entry.formIDStr);
		entry.source = "INI";
		spellAllowlist.push_back(std::move(entry));
	}

	// Load JSON sidecar files
	namespace fs = std::filesystem;
	fs::path jsonDir(kSpellAllowlistDir);
	if (!fs::exists(jsonDir) || !fs::is_directory(jsonDir)) {
		return;
	}

	for (const auto& dirEntry : fs::directory_iterator(jsonDir)) {
		if (!dirEntry.is_regular_file()) continue;
		if (dirEntry.path().extension() != ".json") continue;

		std::ifstream file(dirEntry.path());
		if (!file.is_open()) continue;

		std::string content((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());

		std::string filename = dirEntry.path().filename().string();

		// Simple JSON array parsing (look for objects within SpellAllowlist array)
		auto arrPos = content.find("\"SpellAllowlist\"");
		if (arrPos == std::string::npos) continue;

		auto bracketStart = content.find('[', arrPos);
		if (bracketStart == std::string::npos) continue;
		auto bracketEnd = content.find(']', bracketStart);
		if (bracketEnd == std::string::npos) continue;

		std::string arrContent = content.substr(bracketStart + 1, bracketEnd - bracketStart - 1);

		// Parse each object {...} in the array
		std::size_t searchPos = 0;
		while (true) {
			auto objStart = arrContent.find('{', searchPos);
			if (objStart == std::string::npos) break;
			auto objEnd = arrContent.find('}', objStart);
			if (objEnd == std::string::npos) break;

			std::string obj = arrContent.substr(objStart, objEnd - objStart + 1);
			searchPos = objEnd + 1;

			std::string plugin = SimpleJsonValue(obj, "Plugin");
			std::string formId = SimpleJsonValue(obj, "FormID");
			std::string comment = SimpleJsonValue(obj, "Comment");

			if (plugin.empty() || formId.empty()) continue;

			SpellAllowlistEntry entry;
			entry.pluginName = plugin;
			entry.formIDStr = formId;
			entry.comment = comment;
			entry.source = filename;
			spellAllowlist.push_back(std::move(entry));
		}

		logger::info("HeadshotsKill: loaded spell allowlist JSON '{}'", filename);
	}
}

// =============================================================================
// Race Config JSON loading (mod author files, read-only)
// =============================================================================

void Settings::LoadRaceConfigJSON()
{
	raceConfig.clear();

	namespace fs = std::filesystem;
	fs::path jsonDir(kRaceConfigDir);
	if (!fs::exists(jsonDir) || !fs::is_directory(jsonDir)) {
		return;
	}

	for (const auto& dirEntry : fs::directory_iterator(jsonDir)) {
		if (!dirEntry.is_regular_file()) continue;
		if (dirEntry.path().extension() != ".json") continue;

		std::ifstream file(dirEntry.path());
		if (!file.is_open()) continue;

		std::string content((std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());

		std::string filename = dirEntry.path().filename().string();

		auto parseRaceArray = [&](const std::string& a_arrayName, bool a_blocked) {
			auto arrPos = content.find("\"" + a_arrayName + "\"");
			if (arrPos == std::string::npos) return;

			auto bracketStart = content.find('[', arrPos);
			if (bracketStart == std::string::npos) return;
			auto bracketEnd = content.find(']', bracketStart);
			if (bracketEnd == std::string::npos) return;

			std::string arrContent = content.substr(bracketStart + 1, bracketEnd - bracketStart - 1);

			std::size_t searchPos = 0;
			while (true) {
				auto objStart = arrContent.find('{', searchPos);
				if (objStart == std::string::npos) break;
				auto objEnd = arrContent.find('}', objStart);
				if (objEnd == std::string::npos) break;

				std::string obj = arrContent.substr(objStart, objEnd - objStart + 1);
				searchPos = objEnd + 1;

				std::string race = SimpleJsonValue(obj, "Race");
				if (race.empty()) continue;

				RaceConfigEntry entry;
				entry.raceEditorID = race;
				entry.source = filename;
				entry.blocked = a_blocked;
				entry.comment = SimpleJsonValue(obj, "Comment");

				std::string chanceStr = SimpleJsonValue(obj, "Chance");
				if (!chanceStr.empty()) {
					try { entry.chanceOverride = std::stof(chanceStr); } catch (...) {}
				}
				std::string skillStr = SimpleJsonValue(obj, "SkillWeight");
				if (!skillStr.empty()) {
					try { entry.skillWeight = std::stof(skillStr); } catch (...) {}
				}

				raceConfig.push_back(std::move(entry));
			}
		};

		parseRaceArray("Blacklist", true);
		parseRaceArray("Whitelist", false);

		logger::info("HeadshotsKill: loaded race config JSON '{}' ({} entries)", filename, raceConfig.size());
	}
}

// =============================================================================
// User race config (separate INI, editable via UI)
// =============================================================================

void Settings::LoadUserRaceConfig()
{
	userRaceConfig.clear();

	CSimpleIniA ini;
	ini.SetUnicode();
	if (ini.LoadFile(kUserRaceConfigPath) != SI_OK) {
		return;
	}

	CSimpleIniA::TNamesDepend keys;
	ini.GetAllKeys("Blacklist", keys);
	for (const auto& key : keys) {
		RaceConfigEntry entry;
		entry.raceEditorID = key.pItem;
		entry.source = "User";
		entry.blocked = true;
		entry.comment = ini.GetValue("Blacklist", key.pItem, "");
		userRaceConfig.push_back(std::move(entry));
	}

	keys.clear();
	ini.GetAllKeys("Whitelist", keys);
	for (const auto& key : keys) {
		RaceConfigEntry entry;
		entry.raceEditorID = key.pItem;
		entry.source = "User";
		entry.blocked = false;
		entry.comment = ini.GetValue("Whitelist", key.pItem, "");
		userRaceConfig.push_back(std::move(entry));
	}

	keys.clear();
	ini.GetAllKeys("Chances", keys);
	for (const auto& key : keys) {
		const char* val = ini.GetValue("Chances", key.pItem, "");
		if (!val || !val[0]) continue;

		std::string valStr(val);
		float chance = -1.0f;
		float skill = -1.0f;
		auto commaPos = valStr.find(',');
		try {
			chance = std::stof(valStr.substr(0, commaPos));
			if (commaPos != std::string::npos) {
				skill = std::stof(valStr.substr(commaPos + 1));
			}
		} catch (...) {}

		bool found = false;
		for (auto& existing : userRaceConfig) {
			if (_stricmp(existing.raceEditorID.c_str(), key.pItem) == 0) {
				existing.chanceOverride = chance;
				existing.skillWeight = skill;
				found = true;
				break;
			}
		}
		if (!found) {
			RaceConfigEntry entry;
			entry.raceEditorID = key.pItem;
			entry.source = "User";
			entry.blocked = false;
			entry.chanceOverride = chance;
			entry.skillWeight = skill;
			userRaceConfig.push_back(std::move(entry));
		}
	}

	logger::info("HeadshotsKill: loaded {} user race config entries", userRaceConfig.size());
}

void Settings::SaveUserRaceConfig()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	for (const auto& entry : userRaceConfig) {
		if (entry.blocked) {
			ini.SetValue("Blacklist", entry.raceEditorID.c_str(),
				entry.comment.c_str());
		} else {
			ini.SetValue("Whitelist", entry.raceEditorID.c_str(),
				entry.comment.c_str());
		}
		if (entry.chanceOverride >= 0.0f) {
			char buf[64];
			if (entry.skillWeight >= 0.0f) {
				snprintf(buf, sizeof(buf), "%.1f,%.2f", entry.chanceOverride, entry.skillWeight);
			} else {
				snprintf(buf, sizeof(buf), "%.1f", entry.chanceOverride);
			}
			ini.SetValue("Chances", entry.raceEditorID.c_str(), buf);
		}
	}

	namespace fs = std::filesystem;
	fs::path dir = fs::path(kUserRaceConfigPath).parent_path();
	if (!fs::exists(dir)) {
		fs::create_directories(dir);
	}

	if (ini.SaveFile(kUserRaceConfigPath) != SI_OK) {
		logger::error("Failed to save user race config");
	}
}

bool Settings::IsRaceBlockedByConfig(const std::string& a_edid) const
{
	// User entries take priority over JSON entries
	for (const auto& entry : userRaceConfig) {
		if (_stricmp(entry.raceEditorID.c_str(), a_edid.c_str()) == 0) {
			return entry.blocked;
		}
	}
	for (const auto& entry : raceConfig) {
		if (_stricmp(entry.raceEditorID.c_str(), a_edid.c_str()) == 0) {
			return entry.blocked;
		}
	}
	return false;
}

float Settings::GetRaceChanceOverride(const std::string& a_edid) const
{
	for (const auto& entry : userRaceConfig) {
		if (_stricmp(entry.raceEditorID.c_str(), a_edid.c_str()) == 0 &&
		    entry.chanceOverride >= 0.0f) {
			return entry.chanceOverride;
		}
	}
	for (const auto& entry : raceConfig) {
		if (_stricmp(entry.raceEditorID.c_str(), a_edid.c_str()) == 0 &&
		    entry.chanceOverride >= 0.0f) {
			return entry.chanceOverride;
		}
	}
	return -1.0f;
}

float Settings::GetRaceSkillWeightOverride(const std::string& a_edid) const
{
	for (const auto& entry : userRaceConfig) {
		if (_stricmp(entry.raceEditorID.c_str(), a_edid.c_str()) == 0 &&
		    entry.skillWeight >= 0.0f) {
			return entry.skillWeight;
		}
	}
	for (const auto& entry : raceConfig) {
		if (_stricmp(entry.raceEditorID.c_str(), a_edid.c_str()) == 0 &&
		    entry.skillWeight >= 0.0f) {
			return entry.skillWeight;
		}
	}
	return -1.0f;
}
