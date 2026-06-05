#include "Menu.h"
#include "DismemberingFrameworkAPI.h"
#include "HeadshotLogic.h"
#include "Hooks.h"
#include "PlayerHelmetTracker.h"
#include "Settings.h"
#include "render/D3DContext.h"
#include "render/DrawHandler.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Menu
{
	static void EnsureWindowSize()
	{
		static bool done = false;
		if (!done) {
			ImGui::SetWindowSize(ImVec2(620.0f, 720.0f), ImGuiCond_Once);
			done = true;
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::warn("SKSE Menu Framework not found; install for in-game options");
			return;
		}
		SKSEMenuFramework::SetSection("Headshots Kill");
		SKSEMenuFramework::AddSectionItem("General", RenderGeneral);
		SKSEMenuFramework::AddSectionItem("Races & Chances", RenderRaces);
		SKSEMenuFramework::AddSectionItem("Helmet (NPC)", RenderHelmet);
		SKSEMenuFramework::AddSectionItem("Helmet (Player)", RenderPlayerHelmet);
		SKSEMenuFramework::AddSectionItem("Spells", RenderSpells);
		SKSEMenuFramework::AddSectionItem("Debug", RenderDebug);
		logger::info("HeadshotsKill: registered with SKSE Menu Framework");
	}

	// =========================================================================
	// General
	// =========================================================================
	void __stdcall RenderGeneral()
	{
		EnsureWindowSize();
		auto* s = Settings::GetSingleton();

		ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "General Settings");
		ImGui::TextWrapped(
			"Core configuration for the Headshots Kill mod. Arrows, bolts, and allowlisted spell "
			"projectiles that hit an NPC's head apply massive (OHKO) damage. These settings control "
			"the global on/off, damage amount, bow draw requirements, and kill sound.");
		ImGui::Spacing();
		ImGui::TextDisabled("Tip: Use the Debug page to test headshot effects without combat.");
		ImGui::Separator();

		if (ImGui::Checkbox("Enable mod", &s->enableMod)) s->Save();
		ImGui::SetItemTooltip("Master switch. When off, the entire headshot system is disabled\nand no projectile impacts are processed.");

		if (ImGui::Checkbox("Debug logging", &s->enableDebugLogging)) s->Save();
		ImGui::SetItemTooltip("Writes detailed per-hit info to the SKSE log file.\nUseful for troubleshooting; disable for normal play to avoid log spam.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Damage");
		if (ImGui::SliderFloat("Kill damage", &s->killDamage, 100.0f, 250000.0f, "%.0f")) {
			s->killDamage = std::max(1.0f, s->killDamage);
			s->Save();
		}
		ImGui::SetItemTooltip("Raw damage applied on a successful headshot OHKO.\nShould exceed any NPC's max HP to guarantee a kill.\nDefault: 100000.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Bow Draw");
		if (ImGui::Checkbox("Require full draw", &s->requireFullDraw)) s->Save();
		ImGui::SetItemTooltip("When enabled, only fully-drawn bow shots can trigger headshot OHKO.\nPartially-drawn shots are ignored. Crossbows always count as fully drawn.");

		ImGui::BeginDisabled(!s->requireFullDraw);
		if (ImGui::SliderFloat("Full draw threshold", &s->fullDrawThreshold, 0.5f, 1.0f, "%.2f")) {
			s->fullDrawThreshold = std::clamp(s->fullDrawThreshold, 0.5f, 1.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Bow power must reach this value (0.0-1.0) to count as 'fully drawn'.\n1.0 = absolute max charge. 0.75 = slightly forgiving.\nDefault: 0.75.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Kill Sound");
		ImGui::TextDisabled("Plays a custom WAV when an NPC is killed by a headshot.");
		if (ImGui::Checkbox("Play sound on headshot kill", &s->enableHeadshotKillSound)) s->Save();
		ImGui::SetItemTooltip("Plays a WAV file when a headshot OHKO lands.\nThe WAV must be placed in Data/SKSE/Plugins/HeadshotsKill/.");
		ImGui::BeginDisabled(!s->enableHeadshotKillSound);
		if (ImGui::InputText("WAV file", s->headshotKillSoundFile, s->kMaxSoundFileChars)) s->Save();
		ImGui::SetItemTooltip("Filename of the .wav file (e.g. 'headshotKillA.wav').\nPlaced in: Data/SKSE/Plugins/HeadshotsKill/");
		if (ImGui::SliderFloat("Volume##killsound", &s->headshotKillSoundVolume, 0.0f, 1.0f, "%.2f")) {
			s->headshotKillSoundVolume = std::clamp(s->headshotKillSoundVolume, 0.0f, 1.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Playback volume. 0 = silent, 1 = full volume.\nDefault: 1.0.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Victim Filters");
		ImGui::TextDisabled("Who can be killed by headshot OHKO.");
		if (ImGui::Checkbox("Apply OHKO to player & followers", &s->applyToPlayerAndFollowers)) s->Save();
		ImGui::SetItemTooltip("When off, the player and their followers cannot be killed by NPC headshot OHKO.\nNote: The Player Helmet Knockoff system (see its page) works independently of this toggle.");

		int levelGap = s->levelGapThreshold;
		if (ImGui::SliderInt("Max enemy level advantage", &levelGap, 0, 100)) {
			s->levelGapThreshold = levelGap;
			s->Save();
		}
		ImGui::SetItemTooltip("Headshot OHKO is blocked if the target's level exceeds the\nshooter's level by more than this amount.\nPrevents low-level archers from one-shotting high-level enemies.\nDefault: 30.");

		int ess = s->essentialMode;
		const char* essItems[] = { "Skip essentials", "Apply kill damage anyway" };
		if (ImGui::Combo("Essential NPCs", &ess, essItems, 2)) {
			s->essentialMode = std::clamp(ess, 0, 1);
			s->Save();
		}
		ImGui::SetItemTooltip("How to handle essential (unkillable) NPCs:\n- Skip: no headshot damage applied at all\n- Apply: damage is dealt but the NPC enters bleedout instead of dying");

		if (ImGui::Checkbox("Exclude bosses from OHKO", &s->excludeBossFromOHKO)) s->Save();
		ImGui::SetItemTooltip("When enabled, boss enemies are immune to the headshot instakill.\nBosses are detected via the vanilla Location Ref Type 'Boss'\nand the mod keyword 'ActorTypeBoss' (if present).\nDefault: ON.");

		ImGui::BeginDisabled(!s->excludeBossFromOHKO);
		if (ImGui::Checkbox("Trigger critical hit on boss headshot", &s->bossHeadshotCritical)) s->Save();
		ImGui::SetItemTooltip("When a boss is excluded from OHKO, still reward the headshot\nby triggering a guaranteed vanilla critical hit.\nDefault: ON.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Dismembering Framework");
		if (DismemberingFrameworkAPI::g_API) {
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "DF: detected");
		} else {
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "DF: not found");
		}
		ImGui::BeginDisabled(!DismemberingFrameworkAPI::g_API);
		if (ImGui::Checkbox("Enable head dismemberment on OHKO", &s->enableDismemberOnOHKO)) s->Save();
		ImGui::SetItemTooltip("When enabled, humanoid NPCs killed by headshot OHKO will have their\nhead dismembered via the Dismembering Framework.\nRequires DF to be installed and detected.\nDefault: OFF.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "INI Management");
		if (ImGui::Button("Save INI")) {
			s->Save();
			RE::DebugNotification("HeadshotsKill: settings saved");
		}
		ImGui::SetItemTooltip("Write all current settings to HeadshotsKill.ini");
		ImGui::SameLine();
		if (ImGui::Button("Reload INI")) {
			s->Load();
			RE::DebugNotification("HeadshotsKill: settings reloaded");
		}
		ImGui::SetItemTooltip("Re-read settings from HeadshotsKill.ini (discards unsaved changes)");
		ImGui::SameLine();
		if (ImGui::Button("Defaults")) {
			s->ResetToDefaults();
			s->Save();
			RE::DebugNotification("HeadshotsKill: restored defaults");
		}
		ImGui::SetItemTooltip("Reset ALL settings across all pages to their default values and save");
	}

	// =========================================================================
	// Races & Chances (consolidated)
	// =========================================================================

	static RaceConfigEntry* FindOrCreateUserOverride(Settings* s, const std::string& a_edid)
	{
		for (auto& e : s->userRaceConfig) {
			if (_stricmp(e.raceEditorID.c_str(), a_edid.c_str()) == 0) return &e;
		}
		RaceConfigEntry ne;
		ne.raceEditorID = a_edid;
		ne.source = "User";
		ne.blocked = false;
		ne.chanceOverride = -1.0f;
		ne.skillWeight = -1.0f;
		s->userRaceConfig.push_back(std::move(ne));
		return &s->userRaceConfig.back();
	}

	void __stdcall RenderRaces()
	{
		EnsureWindowSize();
		auto* s = Settings::GetSingleton();

		ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Races & Chances");
		ImGui::TextWrapped(
			"All headshot chance configuration lives here. The system works top-to-bottom:");
		ImGui::Spacing();
		ImGui::BulletText("Category Defaults - base chance for each creature type (saved to main INI)");
		ImGui::BulletText("Per-Race Overrides - override a specific race's chance (saved to user INI)");
		ImGui::BulletText("Blacklist/Whitelist - block or allow specific races entirely");
		ImGui::Spacing();
		ImGui::TextDisabled("Priority: Per-race override > Category default. Blacklisted races are always immune.");
		ImGui::Separator();

		// =================================================================
		// Section 1: Category Defaults
		// =================================================================
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Category Defaults");
		ImGui::TextWrapped(
			"Fallback OHKO chances used when no per-race override exists. "
			"Formula: effective %% = base + (archery skill * weight).");
		ImGui::Spacing();

		auto pctSlider = [s](const char* label, float* v, const char* tip) {
			if (ImGui::SliderFloat(label, v, 0.0f, 100.0f, "%.1f%%")) {
				*v = std::clamp(*v, 0.0f, 100.0f);
				s->Save();
			}
			ImGui::SetItemTooltip("%s", tip);
		};
		pctSlider("Humanoid (NPC)", &s->chanceHumanoid,
			"Chance for human/elf/beast race NPCs.\nDefault: 100%%.");
		pctSlider("Small animals", &s->chanceSmallAnimal,
			"Wolves, skeevers, foxes, mudcrabs, small spiders.\nDefault: 100%%.");
		pctSlider("Giants", &s->chanceGiant,
			"Giant race enemies.\nDefault: 15%%.");
		pctSlider("Trolls", &s->chanceTroll,
			"All troll variants.\nDefault: 20%%.");
		pctSlider("Bears", &s->chanceBear,
			"All bear variants.\nDefault: 10%%.");
		pctSlider("Mammoths", &s->chanceMammoth,
			"Mammoths.\nDefault: 8%%.");
		pctSlider("Giant frostbite spiders", &s->chanceGiantSpider,
			"Large frostbite spiders (scale >= threshold).\nDefault: 35%%.");
		pctSlider("Chaurus", &s->chanceChaurus,
			"Chaurus and chaurus hunters.\nDefault: 25%%.");

		if (ImGui::TreeNode("Skill Weights & Spider Threshold")) {
			ImGui::TextWrapped("Archery skill adds (skill * weight) on top of the base chance.");
			auto sk = [s](const char* label, float* v, const char* tip) {
				if (ImGui::SliderFloat(label, v, 0.0f, 2.0f, "%.2f")) {
					*v = std::clamp(*v, 0.0f, 2.0f);
					s->Save();
				}
				ImGui::SetItemTooltip("%s", tip);
			};
			sk("Giant##sk", &s->skillInfluenceGiant, "Default: 0.35");
			sk("Troll##sk", &s->skillInfluenceTroll, "Default: 0.35");
			sk("Bear##sk", &s->skillInfluenceBear, "Default: 0.50");
			sk("Mammoth##sk", &s->skillInfluenceMammoth, "Default: 0.40");
			sk("Giant spider##sk", &s->skillInfluenceGiantSpider, "Default: 0.0");
			sk("Chaurus##sk", &s->skillInfluenceChaurus, "Default: 0.0");

			ImGui::Spacing();
		float spTh = s->giantSpiderScaleThreshold;
		if (ImGui::SliderFloat("Giant spider scale threshold", &spTh, 1.0f, 2.5f, "%.2f")) {
			s->giantSpiderScaleThreshold = std::clamp(spTh, 1.0f, 3.0f);
			s->Save();
		}
			ImGui::SetItemTooltip("Spiders at or above this scale use 'Giant frostbite spiders' chance.\nBelow uses 'Small animals'.\nDefault: 1.15.");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Preview Effective Chances")) {
			static float previewSkill = 80.0f;
			ImGui::SliderFloat("Archery skill##preview", &previewSkill, 0.0f, 100.0f, "%.0f");
			auto calc = [&](float base, float w) { return std::clamp(base + previewSkill * w, 0.0f, 100.0f); };
			ImGui::Text("  Giant: %.1f%%", calc(s->chanceGiant, s->skillInfluenceGiant));
			ImGui::Text("  Troll: %.1f%%", calc(s->chanceTroll, s->skillInfluenceTroll));
			ImGui::Text("  Bear: %.1f%%", calc(s->chanceBear, s->skillInfluenceBear));
			ImGui::Text("  Mammoth: %.1f%%", calc(s->chanceMammoth, s->skillInfluenceMammoth));
			ImGui::Text("  Giant spider: %.1f%%", calc(s->chanceGiantSpider, s->skillInfluenceGiantSpider));
			ImGui::Text("  Chaurus: %.1f%%", calc(s->chanceChaurus, s->skillInfluenceChaurus));
			ImGui::TreePop();
		}

		ImGui::Separator();

		// =================================================================
		// Section 2: Per-Race Overrides
		// =================================================================
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Per-Race Overrides");
		ImGui::TextWrapped(
			"Override chance/skill weight for specific races. These take priority over "
			"the category defaults above. Saved to HeadshotsKill_UserRaces.ini.");
		ImGui::Spacing();

		static const char* kCategoryNames[] = {
			"None (immune)", "Humanoid", "Small Animal", "Giant", "Troll",
			"Bear", "Mammoth", "Giant Spider", "Chaurus"
		};
		static constexpr int kCategoryCount = 9;

		struct BuiltinRace {
			const char* edid;
			const char* label;
			float* baseChance;
			float* baseSkill;
		};
		BuiltinRace builtins[] = {
			{ "GiantRace",            "Giant",               &s->chanceGiant,       &s->skillInfluenceGiant },
			{ "TrollRace",            "Troll",               &s->chanceTroll,       &s->skillInfluenceTroll },
			{ "BearBlackRace",        "Bear",                &s->chanceBear,        &s->skillInfluenceBear },
			{ "MammothRace",          "Mammoth",             &s->chanceMammoth,     &s->skillInfluenceMammoth },
			{ "FrostbiteSpiderRace",  "Giant Frostbite Spider", &s->chanceGiantSpider, &s->skillInfluenceGiantSpider },
			{ "ChaurusRace",          "Chaurus",             &s->chanceChaurus,     &s->skillInfluenceChaurus },
		};

		for (int bi = 0; bi < 6; ++bi) {
			auto& br = builtins[bi];
			ImGui::PushID(1000 + bi);

			// Find existing user override (don't create yet)
			RaceConfigEntry* ue = nullptr;
			for (auto& e : s->userRaceConfig) {
				if (_stricmp(e.raceEditorID.c_str(), br.edid) == 0) {
					ue = &e;
					break;
				}
			}

			float effectiveChance = *br.baseChance;
			float effectiveSkill = *br.baseSkill;
			float userChance = s->GetRaceChanceOverride(br.edid);
			float userSkill = s->GetRaceSkillWeightOverride(br.edid);
			bool hasUserChance = (userChance >= 0.0f);
			bool hasUserSkill = (userSkill >= 0.0f);
			if (hasUserChance) effectiveChance = userChance;
			if (hasUserSkill) effectiveSkill = userSkill;

			int curCat = ue ? ue->categoryOverride : -1;
			bool curUseCat = ue ? ue->useCategoryChance : true;

			if (ImGui::TreeNode(br.label)) {
				ImGui::Text("Category default: %.1f%% chance, %.2f skill weight",
					*br.baseChance, *br.baseSkill);

				int catIdx = curCat + 1;
				char catLabel[64];
				snprintf(catLabel, sizeof(catLabel), "Category##cat%d", bi);
				const char* catPreview = (catIdx <= 0) ? "Auto-detect" : kCategoryNames[catIdx - 1];
				if (ImGui::BeginCombo(catLabel, catPreview)) {
					if (ImGui::Selectable("Auto-detect", catIdx == 0)) {
						auto* u = FindOrCreateUserOverride(s, br.edid);
						u->categoryOverride = -1;
						s->SaveUserRaceConfig();
					}
					for (int ci = 0; ci < kCategoryCount; ++ci) {
						if (ImGui::Selectable(kCategoryNames[ci], catIdx == ci + 1)) {
							auto* u = FindOrCreateUserOverride(s, br.edid);
							u->categoryOverride = ci;
							s->SaveUserRaceConfig();
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Override category for this race.\nAuto-detect uses keyword/name analysis.");

				bool useCat = curUseCat;
				if (ImGui::Checkbox("Use category headshot chance", &useCat)) {
					auto* u = FindOrCreateUserOverride(s, br.edid);
					u->useCategoryChance = useCat;
					if (useCat) {
						u->chanceOverride = -1.0f;
						u->skillWeight = -1.0f;
					}
					s->SaveUserRaceConfig();
				}
				ImGui::SetItemTooltip("When enabled, uses the category's default chance and skill weight.\nDisable to set individual values.");

				if (!curUseCat) {
					float ch = effectiveChance;
					if (ImGui::SliderFloat("Chance", &ch, 0.0f, 100.0f, "%.1f%%")) {
						auto* u = FindOrCreateUserOverride(s, br.edid);
						u->chanceOverride = ch;
						u->useCategoryChance = false;
						s->SaveUserRaceConfig();
					}
					ImGui::SetItemTooltip("OHKO chance for %s.\nEdits are saved as a user override.", br.label);

					float sw = effectiveSkill;
					if (ImGui::SliderFloat("Skill weight", &sw, 0.0f, 2.0f, "%.2f")) {
						auto* u = FindOrCreateUserOverride(s, br.edid);
						u->skillWeight = sw;
						u->useCategoryChance = false;
						s->SaveUserRaceConfig();
					}
					ImGui::SetItemTooltip("Archery skill scaling weight for %s.", br.label);
				} else {
					ImGui::BeginDisabled();
					float ch = *br.baseChance;
					ImGui::SliderFloat("Chance", &ch, 0.0f, 100.0f, "%.1f%%");
					float sw = *br.baseSkill;
					ImGui::SliderFloat("Skill weight", &sw, 0.0f, 2.0f, "%.2f");
					ImGui::EndDisabled();
				}

				if (hasUserChance || hasUserSkill || curCat >= 0 || !curUseCat) {
					if (ImGui::SmallButton("Reset to category default")) {
						for (auto it = s->userRaceConfig.begin(); it != s->userRaceConfig.end(); ++it) {
							if (_stricmp(it->raceEditorID.c_str(), br.edid) == 0) {
								it->chanceOverride = -1.0f;
								it->skillWeight = -1.0f;
								it->categoryOverride = -1;
								it->useCategoryChance = true;
								if (it->blocked == false && it->chanceOverride < 0.0f && it->skillWeight < 0.0f && it->categoryOverride < 0) {
									s->userRaceConfig.erase(it);
								}
								break;
							}
						}
						s->SaveUserRaceConfig();
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		ImGui::Separator();

		// =================================================================
		// Section 3: Blacklists & Whitelists
		// =================================================================
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Blacklist / Whitelist");
		ImGui::TextWrapped(
			"Races can be blocked (immune to headshot) or explicitly allowed. "
			"Sources include mod author JSON files, legacy INI, and your own user entries.");
		ImGui::Spacing();

		// --- JSON (mod author) entries with editable chance overrides ---
		if (!s->raceConfig.empty()) {
			if (ImGui::TreeNode("From JSON (mod author)")) {
				ImGui::TextDisabled("Block/allow status comes from the JSON file. "
					"You can override chances here (saved to your user config).");

				for (int ji = 0; ji < static_cast<int>(s->raceConfig.size()); ++ji) {
					const auto& entry = s->raceConfig[ji];
					ImGui::PushID(2000 + ji);

					const char* tag = entry.blocked ? "[BLOCK]" : "[ALLOW]";
					if (ImGui::TreeNode("", "%s %s (%s)", tag, entry.raceEditorID.c_str(), entry.source.c_str())) {
						if (!entry.comment.empty()) {
							ImGui::TextDisabled("%s", entry.comment.c_str());
						}

						int jsonCat = entry.categoryOverride;
						bool jsonUseCat = entry.useCategoryChance;
						ImGui::Text("JSON category: %s | Use category chance: %s",
							(jsonCat < 0) ? "Auto-detect" : kCategoryNames[jsonCat],
							jsonUseCat ? "Yes" : "No");

						float jsonChance = entry.chanceOverride;
						float userChance = s->GetRaceChanceOverride(entry.raceEditorID);
						float effective = (userChance >= 0.0f) ? userChance : ((jsonChance >= 0.0f) ? jsonChance : 100.0f);

						int catIdx = jsonCat + 1;
						char catLabel[64];
						snprintf(catLabel, sizeof(catLabel), "Category##jcat%d", ji);
						const char* catPreview = (catIdx <= 0) ? "Auto-detect" : kCategoryNames[catIdx - 1];
						if (ImGui::BeginCombo(catLabel, catPreview)) {
							if (ImGui::Selectable("Auto-detect", catIdx == 0)) {
								auto* ue = FindOrCreateUserOverride(s, entry.raceEditorID);
								ue->categoryOverride = -1;
								ue->blocked = entry.blocked;
								s->SaveUserRaceConfig();
							}
							for (int ci = 0; ci < kCategoryCount; ++ci) {
								if (ImGui::Selectable(kCategoryNames[ci], catIdx == ci + 1)) {
									auto* ue = FindOrCreateUserOverride(s, entry.raceEditorID);
									ue->categoryOverride = ci;
									ue->blocked = entry.blocked;
									s->SaveUserRaceConfig();
								}
							}
							ImGui::EndCombo();
						}
						ImGui::SetItemTooltip("Override category. JSON default: %s",
							(jsonCat < 0) ? "Auto-detect" : kCategoryNames[jsonCat]);

						float ch = effective;
						if (ImGui::SliderFloat("Chance", &ch, 0.0f, 100.0f, "%.1f%%")) {
							auto* ue = FindOrCreateUserOverride(s, entry.raceEditorID);
							ue->chanceOverride = ch;
							ue->blocked = entry.blocked;
							s->SaveUserRaceConfig();
						}
						ImGui::SetItemTooltip("OHKO chance override for this race.\nJSON default: %.1f%%",
							jsonChance >= 0.0f ? jsonChance : 100.0f);

						float jsonSkill = entry.skillWeight;
						float userSkill = s->GetRaceSkillWeightOverride(entry.raceEditorID);
						float effSkill = (userSkill >= 0.0f) ? userSkill : ((jsonSkill >= 0.0f) ? jsonSkill : 0.0f);

						float sw = effSkill;
						if (ImGui::SliderFloat("Skill weight", &sw, 0.0f, 2.0f, "%.2f")) {
							auto* ue = FindOrCreateUserOverride(s, entry.raceEditorID);
							ue->skillWeight = sw;
							ue->blocked = entry.blocked;
							s->SaveUserRaceConfig();
						}

						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			ImGui::Separator();
		}

		// --- Legacy INI blocklist (read-only display) ---
		if (!s->raceBlocklist.empty()) {
			if (ImGui::TreeNode("Legacy INI Blocklist (sRaceBlocklist)")) {
				ImGui::TextDisabled("These are from HeadshotsKill.ini [Lists] sRaceBlocklist.");
				ImGui::TextDisabled("Edit the INI directly or use user entries below instead.");
				for (const auto& race : s->raceBlocklist) {
					ImGui::BulletText("%s", race.c_str());
				}
				ImGui::TreePop();
			}
			ImGui::Separator();
		}

		// --- User entries (editable) ---
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "User Race Entries");
		ImGui::TextWrapped(
			"Custom races you've added. These are saved to HeadshotsKill_UserRaces.ini.");

		int removeIdx = -1;
		for (int i = 0; i < static_cast<int>(s->userRaceConfig.size()); ++i) {
			auto& entry = s->userRaceConfig[i];
			ImGui::PushID(3000 + i);

			char blockedLabel[64];
			snprintf(blockedLabel, sizeof(blockedLabel), "%s##mode",
				entry.blocked ? "BLOCK" : "ALLOW");
			if (ImGui::SmallButton(blockedLabel)) {
				entry.blocked = !entry.blocked;
				s->SaveUserRaceConfig();
			}
			ImGui::SetItemTooltip("Toggle between BLOCK (immune) and ALLOW (can be headshot).");
			ImGui::SameLine();

			ImGui::Text("%s", entry.raceEditorID.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("X##rm")) {
				removeIdx = i;
			}
			ImGui::SetItemTooltip("Remove this entry.");

			if (!entry.blocked) {
				int catIdx = entry.categoryOverride + 1;
				char catLabel[64];
				snprintf(catLabel, sizeof(catLabel), "Category##ucat%d", i);
				const char* catPreview = (catIdx <= 0) ? "Auto-detect" : kCategoryNames[catIdx - 1];
				if (ImGui::BeginCombo(catLabel, catPreview)) {
					if (ImGui::Selectable("Auto-detect", catIdx == 0)) {
						entry.categoryOverride = -1;
						s->SaveUserRaceConfig();
					}
					for (int ci = 0; ci < kCategoryCount; ++ci) {
						if (ImGui::Selectable(kCategoryNames[ci], catIdx == ci + 1)) {
							entry.categoryOverride = ci;
							s->SaveUserRaceConfig();
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Override category for this race.\nAuto-detect uses keyword/name analysis.");

				bool useCat = entry.useCategoryChance;
				char useCatLabel[64];
				snprintf(useCatLabel, sizeof(useCatLabel), "Use category chance##uusec%d", i);
				if (ImGui::Checkbox(useCatLabel, &useCat)) {
					entry.useCategoryChance = useCat;
					if (useCat) {
						entry.chanceOverride = -1.0f;
						entry.skillWeight = -1.0f;
					}
					s->SaveUserRaceConfig();
				}
				ImGui::SetItemTooltip("When enabled, uses the category's default chance.\nDisable to set individual values.");

				if (!useCat) {
					float ch = entry.chanceOverride >= 0.0f ? entry.chanceOverride : 100.0f;
					char chLabel[64];
					snprintf(chLabel, sizeof(chLabel), "Chance##u%d", i);
					if (ImGui::SliderFloat(chLabel, &ch, 0.0f, 100.0f, "%.1f%%")) {
						entry.chanceOverride = ch;
						s->SaveUserRaceConfig();
					}
					float sw = entry.skillWeight >= 0.0f ? entry.skillWeight : 0.0f;
					char swLabel[64];
					snprintf(swLabel, sizeof(swLabel), "Skill weight##u%d", i);
					if (ImGui::SliderFloat(swLabel, &sw, 0.0f, 2.0f, "%.2f")) {
						entry.skillWeight = sw;
						s->SaveUserRaceConfig();
					}
				} else {
					ImGui::BeginDisabled();
					float ch = 0.0f;
					char chLabel[64];
					snprintf(chLabel, sizeof(chLabel), "Chance##u%d", i);
					ImGui::SliderFloat(chLabel, &ch, 0.0f, 100.0f, "%.1f%%");
					float sw = 0.0f;
					char swLabel[64];
					snprintf(swLabel, sizeof(swLabel), "Skill weight##u%d", i);
					ImGui::SliderFloat(swLabel, &sw, 0.0f, 2.0f, "%.2f");
					ImGui::EndDisabled();
				}
			}

			ImGui::PopID();
		}
		if (removeIdx >= 0) {
			s->userRaceConfig.erase(s->userRaceConfig.begin() + removeIdx);
			s->SaveUserRaceConfig();
		}

		ImGui::Separator();

		// --- Add new entry ---
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Add Race");
		static char newRaceBuf[128] = "";
		static bool newRaceBlocked = true;
		static float newRaceChance = 100.0f;
		static float newRaceSkill = 0.0f;

		ImGui::InputText("Race Editor ID##addrace", newRaceBuf, sizeof(newRaceBuf));
		ImGui::SetItemTooltip("The race's editor ID (e.g. NordRace, ArgonianRace, MyCustomRace).");

		ImGui::Checkbox("Blocked (immune)##addblocked", &newRaceBlocked);
		ImGui::SetItemTooltip("If checked, this race is immune to headshot OHKO.\nIf unchecked, this race can be headshot.");

		if (!newRaceBlocked) {
			ImGui::SliderFloat("Chance##addch", &newRaceChance, 0.0f, 100.0f, "%.1f%%");
			ImGui::SetItemTooltip("OHKO chance for this race.");
			ImGui::SliderFloat("Skill weight##addsw", &newRaceSkill, 0.0f, 2.0f, "%.2f");
			ImGui::SetItemTooltip("Archery skill scaling weight for this race.");
		}

		if (ImGui::Button("Add Race##addbtn")) {
			if (newRaceBuf[0]) {
				// Update existing entry if race already present, otherwise add new
				RaceConfigEntry* existing = nullptr;
				for (auto& e : s->userRaceConfig) {
					if (_stricmp(e.raceEditorID.c_str(), newRaceBuf) == 0) {
						existing = &e;
						break;
					}
				}
				if (existing) {
					existing->blocked = newRaceBlocked;
					existing->chanceOverride = newRaceBlocked ? -1.0f : newRaceChance;
					existing->skillWeight = newRaceBlocked ? -1.0f : newRaceSkill;
				} else {
					RaceConfigEntry entry;
					entry.raceEditorID = newRaceBuf;
					entry.source = "User";
					entry.blocked = newRaceBlocked;
					entry.chanceOverride = newRaceBlocked ? -1.0f : newRaceChance;
					entry.skillWeight = newRaceBlocked ? -1.0f : newRaceSkill;
					s->userRaceConfig.push_back(std::move(entry));
				}
				s->SaveUserRaceConfig();
				newRaceBuf[0] = '\0';
				newRaceBlocked = true;
				newRaceChance = 100.0f;
				newRaceSkill = 0.0f;
			}
		}
		ImGui::SetItemTooltip("Add this race to your user config.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Keyword Immune List");
		ImGui::TextWrapped(
			"Actors with any of these keywords are always immune to headshot OHKO, "
			"regardless of race config. Edit as comma-separated values.");
		if (ImGui::InputText("Keywords##kwimmune", s->keywordImmuneBuf, Settings::kMaxIniListChars)) {
			s->ParseList(s->keywordImmuneBuf, s->keywordImmuneList);
			s->Save();
		}
		ImGui::SetItemTooltip("Comma-separated keyword list (e.g. ActorTypeUndead,ActorTypeGhost).");

		// =================================================================
		// Dragons
		// =================================================================
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Dragons");
		ImGui::TextWrapped(
			"Dragons require hitting the eye area (between brow nodes) to trigger. "
			"Generic head hits are not enough.");
		ImGui::Spacing();

		if (ImGui::Checkbox("Enable dragon headshots##dragonEnable", &s->enableDragonHeadshots)) {
			s->Save();
		}
		ImGui::SetItemTooltip("Allow instakills/crits on dragons when shooting their eye area.\nDefault: OFF.");

		ImGui::BeginDisabled(!s->enableDragonHeadshots);

		if (ImGui::SliderFloat("Dragon headshot chance##dragonChance", &s->dragonHeadshotChance, 0.0f, 100.0f, "%.1f%%")) {
			s->dragonHeadshotChance = std::clamp(s->dragonHeadshotChance, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance for a valid dragon eye-shot to proc.\nDefault: 15%%.");

		if (ImGui::Checkbox("Only when below HP threshold##dragonHP", &s->dragonRequireHealthThreshold)) {
			s->Save();
		}
		ImGui::SetItemTooltip("Only proc when the dragon's current HP is below this percentage of max.\nDefault: OFF.");

		ImGui::BeginDisabled(!s->dragonRequireHealthThreshold);
		if (ImGui::SliderFloat("HP threshold##dragonHPPct", &s->dragonHealthThresholdPercent, 1.0f, 100.0f, "%.0f%%")) {
			s->dragonHealthThresholdPercent = std::clamp(s->dragonHealthThresholdPercent, 1.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Dragon must be below this %% of max HP for the headshot to proc.\nDefault: 25%%.");
		ImGui::EndDisabled();

		if (ImGui::Checkbox("Trigger critical hit instead of instakill##dragonCrit", &s->dragonTriggerCriticalHit)) {
			s->Save();
		}
		ImGui::SetItemTooltip("Use the vanilla critical hit system instead of dealing OHKO damage.\nDefault: ON.");

		if (ImGui::SliderFloat("Eye hit radius##dragonEyeRadius", &s->dragonEyeHitRadius, 5.0f, 60.0f, "%.0f units")) {
			s->dragonEyeHitRadius = std::clamp(s->dragonEyeHitRadius, 5.0f, 60.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Radius of the spherical hit zone around each eye.\nThe brow nodes are inside the skull, so this needs to be generous\nenough to catch arrows hitting the skin surface.\nDefault: 28.");

		ImGui::EndDisabled();
	}

	// =========================================================================
	// Helmet (NPC)
	// =========================================================================
	void __stdcall RenderHelmet()
	{
		EnsureWindowSize();
		auto* s = Settings::GetSingleton();

		ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "NPC Helmet System");
		ImGui::TextWrapped(
			"When an NPC wears a protective helmet (armor rating > 0, covers head slot), headshots "
			"knock the helmet off instead of killing them instantly. The next headshot on the now-bare "
			"head is lethal. Bypass options allow skilled shooters or perk holders to ignore helmets.");
		ImGui::Spacing();
		ImGui::TextDisabled("Only heavy/light armor headpieces count. Circlets and hoods are optional.");
		ImGui::Separator();

		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Knock-off");
		if (ImGui::Checkbox("Enable helmet knock-off", &s->enableHelmetKnockoff)) s->Save();
		ImGui::SetItemTooltip("Master switch for NPC helmet knock-off.\nWhen off, helmets are ignored and headshots always OHKO.");

		ImGui::BeginDisabled(!s->enableHelmetKnockoff);
		if (ImGui::SliderFloat("Knockoff chance##npc", &s->helmetKnockoffChance, 0.0f, 100.0f, "%.0f%%")) {
			s->helmetKnockoffChance = std::clamp(s->helmetKnockoffChance, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance that a headshot knocks the helmet off.\nAt 100%%, every headshot removes the helmet.\nAt lower values, the helmet may 'absorb' the hit.\nDefault: 100%%.");

		if (ImGui::Checkbox("Allow circlet/clothing knock-off", &s->knockoffCirclets)) s->Save();
		ImGui::SetItemTooltip("When on, non-armor headpieces (circlets, hoods) can also be knocked off.\nWhen off, only rated armor counts as a 'protective helmet'.");

		if (ImGui::SliderFloat("Drop linear impulse##npc", &s->helmetDropLinearImpulse, 0.0f, 30.0f, "%.1f")) s->Save();
		ImGui::SetItemTooltip("How hard the helmet flies away from the NPC when knocked off.\n0 = drops straight down (gravity only).\nHigher = more dramatic launch.\nDefault: 3.0.");

		if (ImGui::SliderFloat("Drop angular impulse##npc", &s->helmetDropAngularImpulse, 0.0f, 1.0f, "%.2f")) s->Save();
		ImGui::SetItemTooltip("Spin applied to the helmet when knocked off.\n0 = no spin. Higher = tumbles more.\nDefault: 0.1.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Melee Helmet Knockoff");
		if (HeadshotLogic::IsPrecisionActive()) {
			ImGui::TextDisabled("Precision detected: melee hits use accurate body part detection for the head.");
		} else {
			ImGui::TextDisabled("Melee weapon hits have a flat chance to knock helmets off (no body part detection without Precision).");
		}
		if (ImGui::Checkbox("Enable melee helmet knockoff##npc", &s->enableMeleeHelmetKnockoff)) s->Save();
		ImGui::SetItemTooltip("When on, melee hits to the head have a chance to knock off NPC helmets.\nThis only removes the helmet -- it does NOT apply headshot OHKO damage.\n\nWith Precision installed: uses actual hit position for head detection.\nWithout Precision: uses a flat %% chance per swing.");

		ImGui::BeginDisabled(!s->enableMeleeHelmetKnockoff);
		if (ImGui::SliderFloat("1H knockoff chance##npc", &s->meleeKnockoffChance1H, 0.0f, 100.0f, "%.0f%%")) {
			s->meleeKnockoffChance1H = std::clamp(s->meleeKnockoffChance1H, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance for one-handed weapons (sword, axe, mace, dagger)\nto knock off the helmet on a head hit.\nDefault: 10%%.");

		if (ImGui::SliderFloat("2H knockoff chance##npc", &s->meleeKnockoffChance2H, 0.0f, 100.0f, "%.0f%%")) {
			s->meleeKnockoffChance2H = std::clamp(s->meleeKnockoffChance2H, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance for two-handed weapons (greatsword, battleaxe/warhammer)\nto knock off the helmet on a head hit.\nDefault: 20%%.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Knockoff Scaling");
		ImGui::TextWrapped(
			"Adjusts the effective knockoff chance based on the helmet's weight and the "
			"attacker's melee skill. Formula: effective = base - (weight * penalty) + (skill * factor).");
		ImGui::Spacing();

		if (ImGui::Checkbox("Scale by helmet weight##npc", &s->enableWeightScaling)) s->Save();
		ImGui::SetItemTooltip("Heavier helmets are harder to knock off.\nLeather (~2 wt) = -6%%, Steel (~6 wt) = -18%%, Daedric (~12 wt) = -36%%.\nDefault: ON.");

		ImGui::BeginDisabled(!s->enableWeightScaling);
		if (ImGui::SliderFloat("Weight penalty per unit##npc", &s->weightPenaltyPerUnit, 0.0f, 10.0f, "%.1f")) {
			s->weightPenaltyPerUnit = std::clamp(s->weightPenaltyPerUnit, 0.0f, 10.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Each unit of helmet weight reduces knockoff chance by this amount.\nExample: weight=6, penalty=3 -> -18%% chance.\nDefault: 3.0.");
		ImGui::EndDisabled();

		if (ImGui::Checkbox("Scale by attacker skill (melee only)##npc", &s->enableMeleeSkillScaling)) s->Save();
		ImGui::SetItemTooltip("Higher 1H/2H skill increases knockoff chance on melee hits.\nDoes NOT apply to projectile knockoffs.\nDefault: ON.");

		ImGui::BeginDisabled(!s->enableMeleeSkillScaling);
		if (ImGui::SliderFloat("Skill bonus factor##npc", &s->meleeSkillBonusFactor, 0.0f, 1.0f, "%.2f")) {
			s->meleeSkillBonusFactor = std::clamp(s->meleeSkillBonusFactor, 0.0f, 1.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Each point of 1H or 2H skill adds (skill * factor) to knockoff chance.\nExample: skill=80, factor=0.2 -> +16%% chance.\nDefault: 0.2.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Bypass Conditions");
		ImGui::TextWrapped("Bypass lets headshots ignore the helmet entirely (straight to OHKO).");

		if (ImGui::InputText("Bypass perk (Plugin.esp:0xID)", s->helmetBypassPerkStr, s->kMaxFormIDStrChars)) {
			s->Save();
			s->ResolveFormIDs();
		}
		ImGui::SetItemTooltip("If the shooter has this perk, their headshots bypass helmets entirely.\nFormat: PluginName.esp:0xFormID (e.g. Skyrim.esm:0x12345).\nLeave empty to disable perk bypass.");
		if (s->helmetBypassPerkStr[0]) {
			ImGui::SameLine();
			if (s->helmetBypassPerkForm) {
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "OK");
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "not found");
			}
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Or pick from loaded plugins:");
		{
			static int selectedPluginIdx = 0;
			static int selectedPerkIdx = 0;
			static std::vector<std::string> pluginNames;
			static std::vector<std::pair<RE::FormID, std::string>> filteredPerks;
			static bool needRefresh = true;
			static char pluginFilter[128] = "";
			static char perkFilter[128] = "";

			if (needRefresh) {
				pluginNames.clear();
				auto* dh = RE::TESDataHandler::GetSingleton();
				if (dh) {
					for (auto* file : dh->files) {
						if (file && file->fileName[0]) {
							pluginNames.push_back(file->fileName);
						}
					}
				}
				needRefresh = false;
				selectedPluginIdx = 0;
				filteredPerks.clear();
				pluginFilter[0] = '\0';
				perkFilter[0] = '\0';
			}

			if (ImGui::Button("Refresh plugins##perk")) needRefresh = true;
			ImGui::SetItemTooltip("Re-scan loaded plugins");
			ImGui::SameLine();

			if (!pluginNames.empty()) {
				int prevPlugin = selectedPluginIdx;
				const char* pluginPreview = (selectedPluginIdx >= 0 && selectedPluginIdx < static_cast<int>(pluginNames.size()))
					? pluginNames[static_cast<std::size_t>(selectedPluginIdx)].c_str() : "";
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::BeginCombo("Plugin##perk", pluginPreview)) {
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
					ImGui::InputTextWithHint("##pluginSearch", "Search...", pluginFilter, sizeof(pluginFilter));
					for (int i = 0; i < static_cast<int>(pluginNames.size()); ++i) {
						if (pluginFilter[0]) {
							bool match = false;
							const char* hay = pluginNames[static_cast<std::size_t>(i)].c_str();
							const char* needle = pluginFilter;
							for (const char* h = hay; *h; ++h) {
								const char* a = h;
								const char* b = needle;
								while (*a && *b && ((*a | 32) == (*b | 32))) { ++a; ++b; }
								if (!*b) { match = true; break; }
							}
							if (!match) continue;
						}
						const bool selected = (i == selectedPluginIdx);
						if (ImGui::Selectable(pluginNames[static_cast<std::size_t>(i)].c_str(), selected)) {
							selectedPluginIdx = i;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Choose the .esp/.esm/.esl that contains the perk (type to filter)");

				if (selectedPluginIdx != prevPlugin || filteredPerks.empty()) {
					filteredPerks.clear();
					selectedPerkIdx = 0;
					perkFilter[0] = '\0';
					if (selectedPluginIdx >= 0 && selectedPluginIdx < static_cast<int>(pluginNames.size())) {
						auto* dh = RE::TESDataHandler::GetSingleton();
						if (dh) {
							const auto& pname = pluginNames[static_cast<std::size_t>(selectedPluginIdx)];
							for (auto* perk : dh->GetFormArray<RE::BGSPerk>()) {
								if (!perk) continue;
								auto* srcFile = perk->GetFile(0);
								if (!srcFile || pname != srcFile->fileName) continue;
								RE::FormID localID = perk->GetLocalFormID();
								const char* edid = perk->GetFormEditorID();
								std::string label;
								if (edid && edid[0]) {
									char buf[16]; snprintf(buf, sizeof(buf), "%X", localID);
									label = std::string(edid) + " (0x" + buf + ")";
								} else {
									label = perk->GetName();
									if (label.empty()) label = "(unnamed)";
									char buf[16]; snprintf(buf, sizeof(buf), "%X", localID);
									label += " (0x" + std::string(buf) + ")";
								}
								filteredPerks.push_back({ localID, label });
							}
						}
					}
				}

				if (!filteredPerks.empty()) {
					const char* perkPreview = (selectedPerkIdx >= 0 && selectedPerkIdx < static_cast<int>(filteredPerks.size()))
						? filteredPerks[static_cast<std::size_t>(selectedPerkIdx)].second.c_str() : "";
					ImGui::SetNextItemWidth(300.0f);
					if (ImGui::BeginCombo("Perk##pick", perkPreview)) {
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
						ImGui::InputTextWithHint("##perkSearch", "Search...", perkFilter, sizeof(perkFilter));
						for (int i = 0; i < static_cast<int>(filteredPerks.size()); ++i) {
							if (perkFilter[0]) {
								bool match = false;
								const char* hay = filteredPerks[static_cast<std::size_t>(i)].second.c_str();
								const char* needle = perkFilter;
								for (const char* h = hay; *h; ++h) {
									const char* a = h;
									const char* b = needle;
									while (*a && *b && ((*a | 32) == (*b | 32))) { ++a; ++b; }
									if (!*b) { match = true; break; }
								}
								if (!match) continue;
							}
							const bool selected = (i == selectedPerkIdx);
							if (ImGui::Selectable(filteredPerks[static_cast<std::size_t>(i)].second.c_str(), selected)) {
								selectedPerkIdx = i;
							}
							if (selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("Select a perk from this plugin (type to filter)");

					ImGui::SameLine();
					if (ImGui::Button("Use##perk")) {
						if (selectedPerkIdx >= 0 && selectedPerkIdx < static_cast<int>(filteredPerks.size())) {
							const auto& pname = pluginNames[static_cast<std::size_t>(selectedPluginIdx)];
							RE::FormID localID = filteredPerks[static_cast<std::size_t>(selectedPerkIdx)].first;
							char idBuf[32];
							snprintf(idBuf, sizeof(idBuf), "0x%X", localID);
							std::string combined = pname + ":" + idBuf;
							strncpy_s(s->helmetBypassPerkStr, combined.c_str(), s->kMaxFormIDStrChars - 1);
							s->Save();
							s->ResolveFormIDs();
						}
					}
					ImGui::SetItemTooltip("Set this perk as the helmet bypass perk");
				} else {
					ImGui::TextDisabled("No perks found in selected plugin");
				}
			}
		}

		ImGui::Spacing();
		if (ImGui::Checkbox("Level-based bypass", &s->enableHelmetLevelBypass)) s->Save();
		ImGui::SetItemTooltip("When on, headshots bypass helmets if the target is significantly\nweaker (lower level) than the shooter.");
		ImGui::BeginDisabled(!s->enableHelmetLevelBypass);
		{
			int th = s->helmetLevelBypassThreshold;
			if (ImGui::SliderInt("Level gap required##bypass", &th, 1, 50)) {
				s->helmetLevelBypassThreshold = th;
				s->Save();
			}
			ImGui::SetItemTooltip("Shooter must be this many levels above the target\nfor the level bypass to activate.\nDefault: 10.");
		}
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Bow Charge Plus Integration");
		ImGui::TextDisabled("Requires 'Bow Charge Plus' mod to be installed.");
		if (s->bowChargePlusDetected) {
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Status: Bow Charge Plus DETECTED");
		} else {
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Status: Bow Charge Plus NOT detected");
			ImGui::SetItemTooltip("Install 'Bow Charge Plus.esp' for these settings to take effect.\nYou can still configure them ahead of time.");
		}

		if (ImGui::Checkbox("Enable full-charge penetration", &s->enableBCPPenetration)) s->Save();
		ImGui::SetItemTooltip("At maximum Bow Charge Plus charge level (red glow),\nadds a skill-based chance to punch through helmets\nwithout needing to knock them off first.");

		ImGui::BeginDisabled(!s->enableBCPPenetration);
		if (ImGui::SliderFloat("Base penetration %##bcp", &s->bcpBasePenetrationChance, 0.0f, 100.0f, "%.1f%%")) {
			s->bcpBasePenetrationChance = std::clamp(s->bcpBasePenetrationChance, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Base chance to penetrate a helmet at full BCP charge.\nArchery skill bonus is added on top.\nDefault: 20%%.");

		if (ImGui::SliderFloat("Skill threshold##bcp", &s->bcpSkillThreshold, 0.0f, 100.0f, "%.0f")) {
			s->bcpSkillThreshold = std::clamp(s->bcpSkillThreshold, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Archery skill below this adds nothing to penetration chance.\nOnly skill points ABOVE this contribute.\nDefault: 50.");

		if (ImGui::SliderFloat("Skill scale factor##bcp", &s->bcpSkillScaleFactor, 0.0f, 2.0f, "%.2f")) {
			s->bcpSkillScaleFactor = std::clamp(s->bcpSkillScaleFactor, 0.0f, 2.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Each archery point above threshold adds this %% to penetration chance.\nFormula: base + (skill - threshold) * factor\nDefault: 0.5.");

		ImGui::Spacing();
		ImGui::TextDisabled("Preview Calculator");
		{
			static float previewSkill = 80.0f;
			ImGui::SliderFloat("Preview archery skill##bcp", &previewSkill, 0.0f, 100.0f, "%.0f");
			ImGui::SetItemTooltip("Adjust to see how archery skill affects helmet penetration chance.");
			const float bonus = std::max(0.0f, previewSkill - s->bcpSkillThreshold) * s->bcpSkillScaleFactor;
			const float total = std::clamp(s->bcpBasePenetrationChance + bonus, 0.0f, 100.0f);
			ImGui::Text("  -> Penetration chance at skill %.0f: %.1f%%", previewSkill, total);
		}
		ImGui::EndDisabled();
	}

	// =========================================================================
	// Helmet (Player)
	// =========================================================================
	void __stdcall RenderPlayerHelmet()
	{
		EnsureWindowSize();
		auto* s = Settings::GetSingleton();

		ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Player Helmet Knock-off System");
		ImGui::TextWrapped(
			"When an enemy headshots the player, this system can knock off your helmet, reduce your HP, "
			"and create a tracking quest to recover it. The dropped helmet glows and appears on your compass. "
			"If you get headshotted again before recovering it, you may be killed instantly (cooldown kill).");
		ImGui::Spacing();
		ImGui::TextDisabled("This system operates independently of the NPC OHKO toggle on the General page.");
		ImGui::Separator();

		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Core");
		if (ImGui::Checkbox("Enable Player Helmet Knockoff", &s->enablePlayerHelmetKnockoff)) s->Save();
		ImGui::SetItemTooltip("Master switch for the player helmet system.\nWhen off, enemy headshots on the player have no special effect.");

		ImGui::BeginDisabled(!s->enablePlayerHelmetKnockoff);

		if (ImGui::SliderFloat("Knockoff Chance (%)##player", &s->playerHelmetKnockoffChance, 0.0f, 100.0f, "%.0f%%")) {
			s->playerHelmetKnockoffChance = std::clamp(s->playerHelmetKnockoffChance, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance that an enemy headshot knocks your helmet off.\n100%% = every headshot removes it.\nLower = helmet may survive the hit.\nDefault: 50%%.");

		if (ImGui::SliderFloat("Health Reduction (%)##player", &s->playerHealthReductionPercent, 1.0f, 100.0f, "%.0f%%")) {
			s->playerHealthReductionPercent = std::clamp(s->playerHealthReductionPercent, 1.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("When your helmet is knocked off, your HP is set to this %% of current HP.\nExample: at 10%%, if you have 200 HP, you drop to 20 HP.\nAlso applied on bare-head hits (if not killed outright).\nDefault: 10%%.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Lethality");
		if (ImGui::Checkbox("Kill on Bare Headshot", &s->killPlayerOnBareHeadshot)) s->Save();
		ImGui::SetItemTooltip("If you have NO helmet at all (not even a knocked-off one),\nan enemy headshot instantly kills you.\nDefault: off.");

		if (ImGui::SliderFloat("Cooldown Window (sec)##player", &s->playerCooldownSeconds, 1.0f, 300.0f, "%.0f s")) {
			s->playerCooldownSeconds = std::clamp(s->playerCooldownSeconds, 1.0f, 300.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("After your helmet is knocked off, this is the danger window.\nIf you are headshotted again within this time, you die instantly\n(if 'Enable Cooldown Kill' is on).\nDefault: 30 seconds.");

		if (ImGui::Checkbox("Enable Cooldown Kill", &s->enableCooldownKill)) s->Save();
		ImGui::SetItemTooltip("When on, headshots during the cooldown window after knockoff are lethal.\nWhen off, you just take HP reduction instead.\nDefault: on.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Physics");
		if (ImGui::SliderFloat("Drop Impulse##player", &s->playerHelmetDropImpulse, 0.0f, 30.0f, "%.1f")) {
			s->playerHelmetDropImpulse = std::clamp(s->playerHelmetDropImpulse, 0.0f, 30.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("How hard the helmet flies off the player's head.\n0 = drops at feet. Higher = flies further away.\nDefault: 5.0.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Melee Helmet Knockoff");
		if (HeadshotLogic::IsPrecisionActive()) {
			ImGui::TextDisabled("Precision detected: enemy melee hits use accurate head detection.");
		} else {
			ImGui::TextDisabled("Enemy melee hits have a flat chance to knock your helmet off (no body part detection without Precision).");
		}
		if (ImGui::Checkbox("Enable melee helmet knockoff##player", &s->enablePlayerMeleeHelmetKnockoff)) s->Save();
		ImGui::SetItemTooltip("When on, NPC melee attacks that hit your head have a chance to knock off your helmet.\nThe same tracking/recovery system applies afterward.\n\nWith Precision installed: uses actual hit position for head detection.\nWithout Precision: uses a flat %% chance per swing.");

		ImGui::BeginDisabled(!s->enablePlayerMeleeHelmetKnockoff);
		if (ImGui::SliderFloat("1H knockoff chance##playermelee", &s->playerMeleeKnockoffChance1H, 0.0f, 100.0f, "%.0f%%")) {
			s->playerMeleeKnockoffChance1H = std::clamp(s->playerMeleeKnockoffChance1H, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance for enemy one-handed weapons to knock your helmet off.\nDefault: 10%%.");

		if (ImGui::SliderFloat("2H knockoff chance##playermelee", &s->playerMeleeKnockoffChance2H, 0.0f, 100.0f, "%.0f%%")) {
			s->playerMeleeKnockoffChance2H = std::clamp(s->playerMeleeKnockoffChance2H, 0.0f, 100.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Chance for enemy two-handed weapons to knock your helmet off.\nDefault: 20%%.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Knockoff Scaling");
		ImGui::TextWrapped(
			"Adjusts the effective knockoff chance based on helmet weight and attacker skill. "
			"Formula: effective = base - (weight * penalty) + (skill * factor).");
		ImGui::Spacing();

		if (ImGui::Checkbox("Scale by helmet weight##player", &s->enablePlayerWeightScaling)) s->Save();
		ImGui::SetItemTooltip("Heavier helmets are harder to knock off.\nLeather (~2 wt) = -6%%, Steel (~6 wt) = -18%%, Daedric (~12 wt) = -36%%.\nDefault: ON.");

		ImGui::BeginDisabled(!s->enablePlayerWeightScaling);
		if (ImGui::SliderFloat("Weight penalty per unit##player", &s->playerWeightPenaltyPerUnit, 0.0f, 10.0f, "%.1f")) {
			s->playerWeightPenaltyPerUnit = std::clamp(s->playerWeightPenaltyPerUnit, 0.0f, 10.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Each unit of helmet weight reduces knockoff chance by this amount.\nExample: weight=6, penalty=3 -> -18%% chance.\nDefault: 3.0.");
		ImGui::EndDisabled();

		if (ImGui::Checkbox("Scale by attacker skill (melee only)##player", &s->enablePlayerMeleeSkillScaling)) s->Save();
		ImGui::SetItemTooltip("Higher attacker 1H/2H skill increases knockoff chance on melee hits.\nDoes NOT apply to projectile knockoffs.\nDefault: ON.");

		ImGui::BeginDisabled(!s->enablePlayerMeleeSkillScaling);
		if (ImGui::SliderFloat("Skill bonus factor##player", &s->playerMeleeSkillBonusFactor, 0.0f, 1.0f, "%.2f")) {
			s->playerMeleeSkillBonusFactor = std::clamp(s->playerMeleeSkillBonusFactor, 0.0f, 1.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Each point of the attacker's 1H or 2H skill adds (skill * factor) to knockoff chance.\nExample: enemy skill=80, factor=0.2 -> +16%% chance.\nDefault: 0.2.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Tracking & Recovery");
		ImGui::TextDisabled("After knockoff, the mod tracks your helmet so you can find it.");
		if (ImGui::Checkbox("Return Helmet on Load", &s->returnHelmetOnLoad)) s->Save();
		ImGui::SetItemTooltip("If you save and reload without picking up the helmet,\nit is automatically returned to your inventory.\nPrevents permanently losing helmets.\nDefault: on.");

		if (ImGui::SliderFloat("Tracking Duration (min)##player", &s->helmetTrackingDurationMinutes, 0.5f, 60.0f, "%.1f")) {
			s->helmetTrackingDurationMinutes = std::clamp(s->helmetTrackingDurationMinutes, 0.5f, 60.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("How long (real-world minutes) the helmet is tracked with\nhighlight and compass marker before tracking expires.\nDefault: 5 minutes.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Visual Tracking");
		if (ImGui::Checkbox("Enable Highlight", &s->enableHelmetHighlight)) s->Save();
		ImGui::SetItemTooltip("Applies a colored glow to the dropped helmet so you can spot it.\nColor is configurable below.");
		ImGui::BeginDisabled(!s->enableHelmetHighlight);
		if (ImGui::SliderFloat("R##hlR", &s->highlightR, 0.0f, 1.0f, "%.2f")) s->Save();
		ImGui::SetItemTooltip("Red component of the helmet highlight glow (0-1).");
		if (ImGui::SliderFloat("G##hlG", &s->highlightG, 0.0f, 1.0f, "%.2f")) s->Save();
		ImGui::SetItemTooltip("Green component of the helmet highlight glow (0-1).");
		if (ImGui::SliderFloat("B##hlB", &s->highlightB, 0.0f, 1.0f, "%.2f")) s->Save();
		ImGui::SetItemTooltip("Blue component of the helmet highlight glow (0-1).");
		if (ImGui::SliderFloat("Alpha##hlA", &s->highlightAlpha, 0.0f, 1.0f, "%.2f")) s->Save();
		ImGui::SetItemTooltip("Opacity/intensity of the glow effect (0-1).\n0 = invisible, 1 = fully opaque.");
		if (ImGui::Checkbox("Blink##hlBlink", &s->enableHighlightBlink)) s->Save();
		ImGui::SetItemTooltip("Pulses the highlight alpha between 0 and the configured value,\nmaking the helmet glow blink on and off.");
		ImGui::BeginDisabled(!s->enableHighlightBlink);
		if (ImGui::SliderFloat("Blink frequency##hlFreq", &s->highlightBlinkFrequency, 0.1f, 10.0f, "%.1f Hz")) {
			s->highlightBlinkFrequency = std::clamp(s->highlightBlinkFrequency, 0.1f, 10.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("How many full blink cycles per second.\n0.1 = very slow pulse, 10 = rapid strobe.\nDefault: 1.5 Hz.");
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		if (ImGui::Checkbox("Enable Map Marker", &s->enableHelmetMapMarker)) s->Save();
		ImGui::SetItemTooltip("Places a compass/map diamond marker at the dropped helmet's location.\nHelps you navigate back to retrieve it.");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Knockoff Sound");
		if (ImGui::Checkbox("Enable knockoff sound", &s->enablePlayerHelmetKnockoffSound)) s->Save();
		ImGui::SetItemTooltip("Plays a sound when your helmet is knocked off.\nAutomatically selects metal or non-metal sound based on helmet material.\nRandomly picks from available variants (helmetknockoff.wav, helmetknockoff_1.wav, etc.).\nPlace files in: Data/SKSE/Plugins/HeadshotsKill/");

		ImGui::BeginDisabled(!s->enablePlayerHelmetKnockoffSound);
		if (ImGui::SliderFloat("Knockoff sound volume", &s->playerHelmetKnockoffSoundVolume, 0.0f, 1.0f, "%.2f")) {
			s->playerHelmetKnockoffSoundVolume = std::clamp(s->playerHelmetKnockoffSoundVolume, 0.0f, 1.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Volume of the helmet knockoff sound.\n0 = silent, 1 = full volume.\nDefault: 0.8.");
		ImGui::EndDisabled();

		ImGui::EndDisabled();

		// Status readout
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Live Status");
		auto* tracker = PlayerHelmetTracker::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (tracker->IsTracking()) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Helmet Status: TRACKING (dropped)");
			if (tracker->IsInCooldown()) {
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Cooldown: ACTIVE (vulnerable!)");
			} else {
				ImGui::TextDisabled("Cooldown: expired (safe)");
			}
		} else {
			ImGui::TextDisabled("Helmet Status: Not tracking (helmet on head or not knocked off)");
		}
		if (player) {
			ImGui::Text("Wearing protective helmet: %s",
				HeadshotLogic::HasProtectiveHeadArmor(player) ? "YES" : "NO");
			ImGui::Text("Current HP: %.0f / %.0f",
				player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth),
				player->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kHealth));
		}
	}

	// =========================================================================
	// Spells
	// =========================================================================
	void __stdcall RenderSpells()
	{
		EnsureWindowSize();
		auto* s = Settings::GetSingleton();

		ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Spell Allowlist");
		ImGui::TextWrapped(
			"By default, only arrows and bolts can trigger headshot OHKO. This page lets you add "
			"spell projectiles that should also be headshot-capable. Only spells with a projectile "
			"component are shown. Entries can come from the INI or from JSON sidecar files.");
		ImGui::Spacing();
		ImGui::TextDisabled("[OK] = form resolved successfully.  [!] = form not found (mod may not be loaded).");
		ImGui::Separator();

		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Current Allowlist");
		if (!s->spellAllowlist.empty()) {
			ImGui::Text("%d spell(s) registered:", static_cast<int>(s->spellAllowlist.size()));
			ImGui::BeginChild("SpellList", ImVec2(0, 150), true);
			for (std::size_t i = 0; i < s->spellAllowlist.size(); ++i) {
				auto& entry = s->spellAllowlist[i];
				ImGui::PushID(static_cast<int>(i));

				if (entry.valid) {
					ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[OK]");
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[!]");
		}
		ImGui::SameLine();
				if (!entry.resolvedName.empty()) {
					ImGui::Text("%s", entry.resolvedName.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("(%s:%s)", entry.pluginName.c_str(), entry.formIDStr.c_str());
				} else {
					ImGui::Text("%s:%s", entry.pluginName.c_str(), entry.formIDStr.c_str());
				}
				if (!entry.comment.empty()) {
		ImGui::SameLine();
					ImGui::TextDisabled("(%s)", entry.comment.c_str());
				}
				ImGui::SameLine();
				ImGui::TextDisabled("[%s]", entry.source.c_str());

				if (entry.source == "INI") {
					ImGui::SameLine();
					if (ImGui::SmallButton("X")) {
						s->spellAllowlist.erase(s->spellAllowlist.begin() + static_cast<std::ptrdiff_t>(i));
						std::string newBuf;
						for (const auto& e : s->spellAllowlist) {
							if (e.source != "INI") continue;
							if (!newBuf.empty()) newBuf += ',';
							newBuf += e.pluginName + ":" + e.formIDStr;
						}
						strncpy_s(s->spellAllowlistBuf, newBuf.c_str(), sizeof(s->spellAllowlistBuf) - 1);
			s->Save();
						s->ResolveSpellAllowlist();
						ImGui::PopID();
						break;
					}
					ImGui::SetItemTooltip("Remove this spell from the allowlist");
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		} else {
			ImGui::TextDisabled("(empty - no spells registered)");
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Add Spell from Loaded Plugin");
		ImGui::TextDisabled("Select a plugin, then pick a projectile spell to add.");
		{
			static int selectedPluginIdx = 0;
			static int selectedSpellIdx = 0;
			static std::vector<std::string> pluginNames;
			static std::vector<std::pair<RE::FormID, std::string>> filteredSpells;
			static bool needRefresh = true;
			static char spellPluginFilter[128] = "";
			static char spellFilter[128] = "";

			if (needRefresh) {
				pluginNames.clear();
				auto* dh = RE::TESDataHandler::GetSingleton();
				if (dh) {
					for (auto* file : dh->files) {
						if (file && file->fileName[0]) {
							pluginNames.push_back(file->fileName);
						}
					}
				}
				needRefresh = false;
				selectedPluginIdx = 0;
				filteredSpells.clear();
				spellPluginFilter[0] = '\0';
				spellFilter[0] = '\0';
			}

			if (ImGui::Button("Refresh plugins##spell")) needRefresh = true;
			ImGui::SetItemTooltip("Re-scan loaded plugins (use after enabling/disabling mods)");
			ImGui::SameLine();

			if (!pluginNames.empty()) {
				int prevPlugin = selectedPluginIdx;
				const char* pluginPreview = (selectedPluginIdx >= 0 && selectedPluginIdx < static_cast<int>(pluginNames.size()))
					? pluginNames[static_cast<std::size_t>(selectedPluginIdx)].c_str() : "";
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::BeginCombo("Plugin##spell", pluginPreview)) {
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
					ImGui::InputTextWithHint("##spellPluginSearch", "Search...", spellPluginFilter, sizeof(spellPluginFilter));
					for (int i = 0; i < static_cast<int>(pluginNames.size()); ++i) {
						if (spellPluginFilter[0]) {
							bool match = false;
							const char* hay = pluginNames[static_cast<std::size_t>(i)].c_str();
							const char* needle = spellPluginFilter;
							for (const char* h = hay; *h; ++h) {
								const char* a = h;
								const char* b = needle;
								while (*a && *b && ((*a | 32) == (*b | 32))) { ++a; ++b; }
								if (!*b) { match = true; break; }
							}
							if (!match) continue;
						}
						const bool selected = (i == selectedPluginIdx);
						if (ImGui::Selectable(pluginNames[static_cast<std::size_t>(i)].c_str(), selected)) {
							selectedPluginIdx = i;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Choose the .esp/.esm/.esl that contains the spell (type to filter)");

				if (selectedPluginIdx != prevPlugin || filteredSpells.empty()) {
					filteredSpells.clear();
					selectedSpellIdx = 0;
					spellFilter[0] = '\0';
					if (selectedPluginIdx >= 0 && selectedPluginIdx < static_cast<int>(pluginNames.size())) {
						auto* dh = RE::TESDataHandler::GetSingleton();
						if (dh) {
							const auto& pname = pluginNames[static_cast<std::size_t>(selectedPluginIdx)];
							for (auto* spell : dh->GetFormArray<RE::SpellItem>()) {
								if (!spell) continue;
								auto* srcFile = spell->GetFile(0);
								if (!srcFile || pname != srcFile->fileName) continue;
								bool hasProj = false;
								for (auto* eff : spell->effects) {
									if (eff && eff->baseEffect && eff->baseEffect->data.projectileBase) {
										hasProj = true;
										break;
									}
								}
								if (!hasProj) continue;
								RE::FormID localID = spell->GetLocalFormID();
								const char* edid = spell->GetFormEditorID();
								std::string label;
								if (edid && edid[0]) {
									char buf[16]; snprintf(buf, sizeof(buf), "%X", localID);
									label = std::string(edid) + " (0x" + buf + ")";
								} else {
									label = spell->GetName();
									if (label.empty()) label = "(unnamed)";
									char buf[16]; snprintf(buf, sizeof(buf), "%X", localID);
									label += " (0x" + std::string(buf) + ")";
								}
								filteredSpells.push_back({ localID, label });
							}
						}
					}
				}

				if (!filteredSpells.empty()) {
					const char* spellPreview = (selectedSpellIdx >= 0 && selectedSpellIdx < static_cast<int>(filteredSpells.size()))
						? filteredSpells[static_cast<std::size_t>(selectedSpellIdx)].second.c_str() : "";
					ImGui::SetNextItemWidth(300.0f);
					if (ImGui::BeginCombo("Spell##pick", spellPreview)) {
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
						ImGui::InputTextWithHint("##spellSearch", "Search...", spellFilter, sizeof(spellFilter));
						for (int i = 0; i < static_cast<int>(filteredSpells.size()); ++i) {
							if (spellFilter[0]) {
								bool match = false;
								const char* hay = filteredSpells[static_cast<std::size_t>(i)].second.c_str();
								const char* needle = spellFilter;
								for (const char* h = hay; *h; ++h) {
									const char* a = h;
									const char* b = needle;
									while (*a && *b && ((*a | 32) == (*b | 32))) { ++a; ++b; }
									if (!*b) { match = true; break; }
								}
								if (!match) continue;
							}
							const bool selected = (i == selectedSpellIdx);
							if (ImGui::Selectable(filteredSpells[static_cast<std::size_t>(i)].second.c_str(), selected)) {
								selectedSpellIdx = i;
							}
							if (selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("Only spells with a projectile effect are listed (type to filter)");

					ImGui::SameLine();
					if (ImGui::Button("Add")) {
						if (selectedSpellIdx >= 0 && selectedSpellIdx < static_cast<int>(filteredSpells.size())) {
							const auto& pname = pluginNames[static_cast<std::size_t>(selectedPluginIdx)];
							RE::FormID localID = filteredSpells[static_cast<std::size_t>(selectedSpellIdx)].first;
							char idBuf[32];
							snprintf(idBuf, sizeof(idBuf), "0x%X", localID);
							std::string current(s->spellAllowlistBuf);
							if (!current.empty()) current += ',';
							current += pname + ":" + idBuf;
							strncpy_s(s->spellAllowlistBuf, current.c_str(), sizeof(s->spellAllowlistBuf) - 1);
							SpellAllowlistEntry entry;
							entry.pluginName = pname;
							entry.formIDStr = idBuf;
							entry.source = "INI";
							s->spellAllowlist.push_back(std::move(entry));
							s->Save();
							s->ResolveSpellAllowlist();
						}
					}
					ImGui::SetItemTooltip("Add this spell to the headshot-capable allowlist");
				} else {
					ImGui::TextDisabled("No projectile spells found in selected plugin");
				}
			}
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Info");
		ImGui::TextWrapped("JSON sidecars: Place .json files in Data/SKSE/Plugins/HeadshotsKill/ to "
			"add spells from other mods without editing the INI. See documentation for format.");
	}

	// =========================================================================
	// Debug
	// =========================================================================
	void __stdcall RenderDebug()
	{
		EnsureWindowSize();
		auto* s = Settings::GetSingleton();

		ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Debug & Testing");
		ImGui::TextWrapped(
			"Tools for testing and troubleshooting the headshot system without needing actual combat. "
			"Enable debug logging to see detailed per-hit information in the SKSE log.");
		ImGui::Separator();

		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Integrations");
		if (HeadshotLogic::IsPrecisionActive()) {
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Precision: Active");
			ImGui::SetItemTooltip("Precision mod detected. Melee helmet knockoffs use accurate hit position for head detection.");
		} else {
			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "Precision: Not detected (flat-chance fallback)");
			ImGui::SetItemTooltip("Precision mod not installed. Melee helmet knockoffs use a flat % chance (no body part detection).\n"
				"Install Precision for accurate melee head-hit detection.");
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Logging");
		if (ImGui::Checkbox("Debug logging##debug", &s->enableDebugLogging)) s->Save();
		ImGui::SetItemTooltip("Writes per-impact details to the SKSE log file:\n"
			"- Impact location, material, bone name\n"
			"- Head detection result (damageLimb, bone match, geometric)\n"
			"- Chance rolls and filter results\n"
			"Log location: Documents/My Games/Skyrim Special Edition/SKSE/HeadshotsKill.log");

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Simulate Player Headshot");
		ImGui::TextWrapped(
			"Triggers the player helmet knockoff logic as if an NPC successfully headshotted you. "
			"Bypasses all projectile/impact checks and goes straight to the effect.");
		ImGui::Spacing();

		ImGui::BeginDisabled(!s->enablePlayerHelmetKnockoff);
		if (ImGui::Button("Simulate Headshot (Player)")) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				SKSE::GetTaskInterface()->AddTask([]() {
					auto* player = RE::PlayerCharacter::GetSingleton();
					auto* settings = Settings::GetSingleton();
					if (!player) return;

					auto* tracker = PlayerHelmetTracker::GetSingleton();

					if (HeadshotLogic::HasProtectiveHeadArmor(player)) {
						HeadshotLogic::SimulatePlayerHelmetKnockoff(player, settings);
					} else if (tracker->IsInCooldown() && settings->enableCooldownKill) {
						float hp = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
						player->AsActorValueOwner()->RestoreActorValue(
							RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -hp);
						RE::DebugNotification("DEBUG: cooldown kill!");
						logger::info("DEBUG: simulated cooldown kill (was {:.0f} HP)", hp);
					} else {
						float currentHP = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
						float targetHP = std::max(1.0f, currentHP * (settings->playerHealthReductionPercent / 100.0f));
						float damage = currentHP - targetHP;
						if (damage > 0.0f) {
							player->AsActorValueOwner()->RestoreActorValue(
								RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -damage);
						}
						tracker->StartCooldown();
						RE::DebugNotification("DEBUG: bare head hit + cooldown started");
						logger::info("DEBUG: simulated bare head hit, HP {:.0f} -> {:.0f}, cooldown started", currentHP, targetHP);
					}
				});
			}
		}
		ImGui::SetItemTooltip("Click to simulate being headshotted right now.\n"
			"- If wearing helmet: knocks it off + HP reduction\n"
			"- If in cooldown: kills you (if cooldown kill enabled)\n"
			"- If bare head: applies HP reduction\n"
			"Must be in-game (not main menu).");
		ImGui::EndDisabled();
		if (!s->enablePlayerHelmetKnockoff) {
			ImGui::TextDisabled("(Enable 'Player Helmet Knockoff' on the Helmet (Player) page first)");
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Live State Readout");
		ImGui::TextDisabled("Current plugin state (updates in real-time):");
		auto* tracker = PlayerHelmetTracker::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();

		ImGui::Text("Helmet tracking: %s", tracker->IsTracking() ? "ACTIVE" : "inactive");
		ImGui::SetItemTooltip("Whether a knocked-off helmet is currently being tracked in the world.");
		ImGui::Text("Cooldown active: %s", tracker->IsInCooldown() ? "YES (danger!)" : "no");
		ImGui::SetItemTooltip("Whether the player is currently in the post-knockoff danger window.");

		if (player) {
			ImGui::Spacing();
			ImGui::Text("Protective helmet equipped: %s",
				HeadshotLogic::HasProtectiveHeadArmor(player) ? "YES" : "NO");
			ImGui::SetItemTooltip("Whether the player currently wears head armor with rating > 0.");
			ImGui::Text("Player HP: %.0f / %.0f (%.0f%%)",
				player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth),
				player->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kHealth),
				player->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kHealth) > 0
					? (player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) /
					   player->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kHealth) * 100.0f)
					: 0.0f);
			ImGui::SetItemTooltip("Current health / base health (percentage).");
			ImGui::Text("Mod enabled: %s", s->enableMod ? "YES" : "NO");
			ImGui::Text("Player helmet system: %s", s->enablePlayerHelmetKnockoff ? "ENABLED" : "disabled");
		} else {
			ImGui::TextDisabled("(Player not loaded - are you on the main menu?)");
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Bypass Perk Test");
		ImGui::TextWrapped(
			"Add/remove the vanilla Steady Hand perk (Archery tree) to test the bypass perk feature. "
			"Set bypass perk to \"Skyrim.esm:0x103ADB\" on the Helmet page first.");
		ImGui::Spacing();

		{
			static constexpr RE::FormID kSteadyHandFormID = 0x103ADB;
			auto* testPlayer = RE::PlayerCharacter::GetSingleton();
			RE::BGSPerk* steadyHand = testPlayer
				? RE::TESForm::LookupByID<RE::BGSPerk>(kSteadyHandFormID) : nullptr;

			bool hasPerk = (testPlayer && steadyHand && testPlayer->HasPerk(steadyHand));
			ImGui::Text("Steady Hand (0x103ADB): %s", hasPerk ? "HAS PERK" : "does not have");

			if (s->helmetBypassPerkForm) {
				bool playerHasBypass = (testPlayer && testPlayer->HasPerk(s->helmetBypassPerkForm));
				ImGui::Text("Configured bypass perk (%s): %s",
					s->helmetBypassPerkStr, playerHasBypass ? "HAS PERK" : "does not have");
			} else if (s->helmetBypassPerkStr[0]) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Bypass perk not resolved: \"%s\"", s->helmetBypassPerkStr);
			} else {
				ImGui::TextDisabled("No bypass perk configured.");
			}

			ImGui::Spacing();
			ImGui::BeginDisabled(!testPlayer || !steadyHand);
			if (ImGui::Button("Add Steady Hand Perk")) {
				if (testPlayer && steadyHand) {
					SKSE::GetTaskInterface()->AddTask([steadyHand]() {
						auto* p = RE::PlayerCharacter::GetSingleton();
						if (p && !p->HasPerk(steadyHand)) {
							p->AddPerk(steadyHand);
							RE::DebugNotification("DEBUG: Added Steady Hand perk");
							logger::info("DEBUG: added Steady Hand perk 0x103ADB");
						}
					});
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove Steady Hand Perk")) {
				if (testPlayer && steadyHand) {
					SKSE::GetTaskInterface()->AddTask([steadyHand]() {
						auto* p = RE::PlayerCharacter::GetSingleton();
						if (p && p->HasPerk(steadyHand)) {
							p->RemovePerk(steadyHand);
							RE::DebugNotification("DEBUG: Removed Steady Hand perk");
							logger::info("DEBUG: removed Steady Hand perk 0x103ADB");
						}
					});
				}
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Add/remove the Steady Hand perk to test the helmet bypass feature.\n"
				"Set bypass perk to \"Skyrim.esm:0x103ADB\" on the Helmet page to test with this perk.");
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "Debug Hit Zones");
		ImGui::TextWrapped("Draws wireframe spheres at the headshot-eligible zone on nearby actors.");
		if (ImGui::Checkbox("Show hit zones##debugHitZones", &s->enableDebugHitZones)) {
			s->Save();
			if (s->enableDebugHitZones) {
				Render::InstallHooks();
				if (Render::HasContext()) {
					DrawHandler::GetSingleton()->Initialize();
				}
			}
		}
		ImGui::SetItemTooltip("Draws wireframe spheres at the headshot-eligible zone on nearby actors.\n"
			"Cyan = head zone, magenta = dragon eye zone.\nRequires a save/load after enabling for the first time.");

		ImGui::BeginDisabled(!s->enableDebugHitZones);
		if (ImGui::SliderFloat("Display radius##debugRadius", &s->debugHitZoneRadius, 500.0f, 5000.0f, "%.0f units")) {
			s->debugHitZoneRadius = std::clamp(s->debugHitZoneRadius, 500.0f, 5000.0f);
			s->Save();
		}
		ImGui::SetItemTooltip("Only draw hit zones for actors within this distance from the player.\nDefault: 2000 units.");
		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "How Head Detection Works");
		ImGui::TextWrapped(
			"1. Engine body part: The game resolves which body part was hit (damageLimb). "
			"If damageLimb == kHead, it's a headshot.\n\n"
			"2. Bone name fallback: If the engine's damageRootNode points to a head bone "
			"(matched against the race's bodyPartData or common head node names), it's a headshot.\n\n"
			"3. Geometric fallback: If no damageRootNode is set, finds the closest skeleton node "
			"to the impact point. If that's a head node, it's a headshot.\n\n"
			"Both methods are OR-ed together for maximum accuracy.");
		ImGui::Spacing();
		ImGui::TextWrapped("Race blocklist & keyword immunity can be configured in HeadshotsKill.ini [Lists] section.");
	}
}
