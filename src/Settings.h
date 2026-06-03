#pragma once

#include "PCH.h"

struct SpellAllowlistEntry
{
	RE::FormID  resolvedFormID{ 0 };
	std::string pluginName;
	std::string formIDStr;
	std::string resolvedName;
	std::string comment;
	std::string source;      // "INI" or filename.json
	bool        valid{ false };
};

struct RaceConfigEntry
{
	std::string raceEditorID;
	std::string source;        // "User" or filename.json
	bool        blocked{ true };  // true=blacklisted, false=whitelisted (allowed)
	bool        useCategoryChance{ true };  // when true, use category defaults for chance/skill
	int         categoryOverride{ -1 };     // -1=auto-detect, 0=None, 1=Humanoid, 2=SmallAnimal, 3=Giant, 4=Troll, 5=Bear, 6=Mammoth, 7=SpiderGiant, 8=Chaurus
	float       chanceOverride{ -1.0f };    // only used when useCategoryChance=false
	float       skillWeight{ -1.0f };       // only used when useCategoryChance=false
	std::string comment;
};

class Settings
{
public:
	static Settings* GetSingleton()
	{
		static Settings instance;
		return &instance;
	}

	void Load();
	void Save();
	void ResetToDefaults();
	void LoadSpellAllowlistJSON();

	bool enableMod{ true };
	bool enableDebugLogging{ false };

	float killDamage{ 99999.0f };

	bool applyToPlayerAndFollowers{ false };

	std::int32_t levelGapThreshold{ 30 };

	std::int32_t essentialMode{ 0 };

	float chanceHumanoid{ 100.0f };
	float chanceSmallAnimal{ 100.0f };
	float chanceGiant{ 15.0f };
	float chanceTroll{ 20.0f };
	float chanceBear{ 10.0f };
	float chanceMammoth{ 8.0f };
	float chanceGiantSpider{ 35.0f };
	float chanceChaurus{ 25.0f };

	float giantSpiderScaleThreshold{ 1.15f };

	float skillInfluenceGiant{ 0.35f };
	float skillInfluenceTroll{ 0.35f };
	float skillInfluenceBear{ 0.50f };
	float skillInfluenceMammoth{ 0.40f };
	float skillInfluenceGiantSpider{ 0.0f };
	float skillInfluenceChaurus{ 0.0f };

	bool enableHelmetKnockoff{ true };

	float helmetKnockoffChance{ 30.0f };
	bool knockoffCirclets{ false };

	float helmetDropLinearImpulse{ 3.0f };
	float helmetDropAngularImpulse{ 0.10f };

	// --- NPC Melee helmet knockoff ---
	bool enableMeleeHelmetKnockoff{ false };
	float meleeKnockoffChance1H{ 10.0f };
	float meleeKnockoffChance2H{ 20.0f };

	// --- NPC Helmet knockoff weight/skill scaling ---
	bool  enableWeightScaling{ true };
	float weightPenaltyPerUnit{ 3.0f };
	bool  enableMeleeSkillScaling{ true };
	float meleeSkillBonusFactor{ 0.2f };

	// --- Full draw requirement ---
	bool requireFullDraw{ true };
	float fullDrawThreshold{ 0.75f };

	// --- Helmet bypass: perk ---
	static constexpr std::size_t kMaxFormIDStrChars = 128;
	char helmetBypassPerkStr[kMaxFormIDStrChars]{};
	RE::BGSPerk* helmetBypassPerkForm{ nullptr };

	// --- Helmet bypass: level ---
	bool enableHelmetLevelBypass{ false };
	std::int32_t helmetLevelBypassThreshold{ 10 };

	// --- Bow Charge Plus ---
	bool bowChargePlusDetected{ false };
	bool enableBCPPenetration{ true };
	float bcpBasePenetrationChance{ 5.0f };
	float bcpSkillThreshold{ 50.0f };
	float bcpSkillScaleFactor{ 0.5f };
	RE::EffectSetting* bcpAttack3Effect{ nullptr };
	RE::TESGlobal* bcpCS3DamageGlobal{ nullptr };

	// --- Spell allowlist ---
	static constexpr std::size_t kMaxSpellAllowlistChars = 8192;
	char spellAllowlistBuf[kMaxSpellAllowlistChars]{};
	std::vector<SpellAllowlistEntry> spellAllowlist;
	std::unordered_set<RE::FormID> spellAllowlistResolved;

	// --- Sound ---
	bool enableHeadshotKillSound{ true };
	static constexpr std::size_t kMaxSoundFileChars = 260;
	char headshotKillSoundFile[kMaxSoundFileChars]{};
	float headshotKillSoundVolume{ 1.0f };

	// --- Player helmet knock-off ---
	bool enablePlayerHelmetKnockoff{ true };
	float playerHelmetKnockoffChance{ 30.0f };
	float playerHealthReductionPercent{ 10.0f };
	bool killPlayerOnBareHeadshot{ false };
	float playerCooldownSeconds{ 30.0f };
	bool enableCooldownKill{ true };
	float playerHelmetDropImpulse{ 3.0f };

	// --- Player Melee helmet knockoff ---
	bool enablePlayerMeleeHelmetKnockoff{ false };
	float playerMeleeKnockoffChance1H{ 10.0f };
	float playerMeleeKnockoffChance2H{ 20.0f };

	// --- Player Helmet knockoff weight/skill scaling ---
	bool  enablePlayerWeightScaling{ true };
	float playerWeightPenaltyPerUnit{ 3.0f };
	bool  enablePlayerMeleeSkillScaling{ true };
	float playerMeleeSkillBonusFactor{ 0.2f };

	bool returnHelmetOnLoad{ true };
	float helmetTrackingDurationMinutes{ 5.0f };
	bool enableHelmetHighlight{ true };
	float highlightR{ 1.0f };
	float highlightG{ 1.0f };
	float highlightB{ 1.0f };
	float highlightAlpha{ 0.6f };
	bool enableHelmetMapMarker{ true };
	bool enablePlayerHelmetKnockoffSound{ true };
	float playerHelmetKnockoffSoundVolume{ 0.8f };
	char playerHelmetKnockoffSoundFile[kMaxSoundFileChars]{}; // legacy, kept for INI compat

	// --- Lists ---
	static constexpr std::size_t kMaxIniListChars = 8192;
	char raceBlocklistBuf[kMaxIniListChars]{};
	char keywordImmuneBuf[kMaxIniListChars]{};

	std::vector<std::string> raceBlocklist;
	std::vector<std::string> keywordImmuneList;

	// --- Race config (JSON + user) ---
	std::vector<RaceConfigEntry> raceConfig;         // aggregated from JSON files
	std::vector<RaceConfigEntry> userRaceConfig;     // user edits (saved to INI)

	bool IsRaceBlockedByConfig(const std::string& a_edid) const;
	float GetRaceChanceOverride(const std::string& a_edid) const;
	float GetRaceSkillWeightOverride(const std::string& a_edid) const;
	void LoadRaceConfigJSON();
	void LoadUserRaceConfig();
	void SaveUserRaceConfig();

	// --- Helpers ---
	void ResolveFormIDs();
	void ResolveSpellAllowlist();
	void ParseList(const char* a_csv, std::vector<std::string>& a_out);
	static RE::TESForm* LookupFormFromString(const std::string& a_str);
};
