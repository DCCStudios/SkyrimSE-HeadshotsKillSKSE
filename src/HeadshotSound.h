#pragma once

#include "PCH.h"

/// Plays the configured headshot-kill WAV (Windows PlaySound from memory, volume-scaled).
/// Safe to call from game thread; defers to SKSE task queue.
void PlayHeadshotKillSound();

/// Plays a player helmet knockoff sound, auto-selecting metal vs non-metal based on armor
/// material and randomly picking among available WAV variants.
void PlayPlayerHelmetKnockoffSound(RE::FormID a_armorFormID);
