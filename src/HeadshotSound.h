#pragma once

/// Plays the configured headshot-kill WAV (Windows PlaySound from memory, volume-scaled).
/// Safe to call from game thread; defers to SKSE task queue.
void PlayHeadshotKillSound();

/// Plays the player helmet knockoff sound (separate WAV from settings).
void PlayPlayerHelmetKnockoffSound();
