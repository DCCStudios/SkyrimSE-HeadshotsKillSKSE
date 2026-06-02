#include "HeadshotSound.h"
#include "Settings.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#ifdef PlaySound
#	undef PlaySound
#endif

namespace
{
#pragma pack(push, 1)
	struct WAVHeader
	{
		char     riff[4];
		std::uint32_t fileSize;
		char     wave[4];
	};

	struct WAVChunkHeader
	{
		char          id[4];
		std::uint32_t size;
	};

	struct WAVFmtChunk
	{
		std::uint16_t audioFormat;
		std::uint16_t numChannels;
		std::uint32_t sampleRate;
		std::uint32_t byteRate;
		std::uint16_t blockAlign;
		std::uint16_t bitsPerSample;
	};
#pragma pack(pop)

	std::vector<std::uint8_t> g_headshotKillAudioBuffer;

	bool LoadWAVWithVolume(const std::filesystem::path& filePath, float volumeScale,
		std::vector<std::uint8_t>& outBuffer)
	{
		std::ifstream file(filePath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			return false;
		}

		const std::streamsize fileSize = file.tellg();
		file.seekg(0, std::ios::beg);

		outBuffer.resize(static_cast<std::size_t>(fileSize));
		if (!file.read(reinterpret_cast<char*>(outBuffer.data()), fileSize)) {
			return false;
		}
		file.close();

		if (outBuffer.size() < sizeof(WAVHeader)) {
			return false;
		}

		auto* header = reinterpret_cast<WAVHeader*>(outBuffer.data());
		if (std::memcmp(header->riff, "RIFF", 4) != 0 || std::memcmp(header->wave, "WAVE", 4) != 0) {
			return false;
		}

		std::size_t pos = sizeof(WAVHeader);
		WAVFmtChunk* fmt = nullptr;
		std::uint8_t* audioData = nullptr;
		std::uint32_t audioDataSize = 0;

		while (pos + sizeof(WAVChunkHeader) <= outBuffer.size()) {
			auto* chunk = reinterpret_cast<WAVChunkHeader*>(outBuffer.data() + pos);

			if (std::memcmp(chunk->id, "fmt ", 4) == 0) {
				fmt = reinterpret_cast<WAVFmtChunk*>(outBuffer.data() + pos + sizeof(WAVChunkHeader));
			} else if (std::memcmp(chunk->id, "data", 4) == 0) {
				audioData = outBuffer.data() + pos + sizeof(WAVChunkHeader);
				audioDataSize = chunk->size;
				break;
			}

			pos += sizeof(WAVChunkHeader) + chunk->size;
			if (pos % 2 != 0) {
				++pos;
			}
		}

		if (!fmt || !audioData || audioDataSize == 0) {
			return false;
		}

		if (fmt->audioFormat != 1) {
			return true;
		}

		if (volumeScale >= 0.99f) {
			return true;
		}

		if (fmt->bitsPerSample == 16) {
			auto* samples = reinterpret_cast<std::int16_t*>(audioData);
			const std::size_t numSamples = audioDataSize / sizeof(std::int16_t);
			for (std::size_t i = 0; i < numSamples; ++i) {
				const float sample = static_cast<float>(samples[i]) * volumeScale;
				samples[i] = static_cast<std::int16_t>(std::clamp(sample, -32768.0f, 32767.0f));
			}
		} else if (fmt->bitsPerSample == 8) {
			for (std::uint32_t i = 0; i < audioDataSize; ++i) {
				const float sample = (static_cast<float>(audioData[i]) - 128.0f) * volumeScale;
				audioData[i] = static_cast<std::uint8_t>(std::clamp(sample + 128.0f, 0.0f, 255.0f));
			}
		} else if (fmt->bitsPerSample == 24) {
			const std::size_t numSamples = audioDataSize / 3;
			for (std::size_t i = 0; i < numSamples; ++i) {
				std::int32_t sample =
					audioData[i * 3] | (audioData[i * 3 + 1] << 8) | (audioData[i * 3 + 2] << 16);
				if (sample & 0x800000) {
					sample |= 0xFF000000;
				}
				const float adjusted = static_cast<float>(sample) * volumeScale;
				const std::int32_t result =
					static_cast<std::int32_t>(std::clamp(adjusted, -8388608.0f, 8388607.0f));
				audioData[i * 3] = result & 0xFF;
				audioData[i * 3 + 1] = (result >> 8) & 0xFF;
				audioData[i * 3 + 2] = (result >> 16) & 0xFF;
			}
		} else if (fmt->bitsPerSample == 32) {
			auto* samples = reinterpret_cast<std::int32_t*>(audioData);
			const std::size_t numSamples = audioDataSize / sizeof(std::int32_t);
			for (std::size_t i = 0; i < numSamples; ++i) {
				const double sample = static_cast<double>(samples[i]) * static_cast<double>(volumeScale);
				samples[i] = static_cast<std::int32_t>(
					std::clamp(sample, -2147483648.0, 2147483647.0));
			}
		}

		return true;
	}

	std::filesystem::path ResolveWavPath(std::string_view a_name)
	{
		std::string fn(a_name);
		while (!fn.empty() && (fn.back() == ' ' || fn.back() == '\t')) {
			fn.pop_back();
		}
		std::size_t i = 0;
		while (i < fn.size() && (fn[i] == ' ' || fn[i] == '\t')) {
			++i;
		}
		if (i > 0) {
			fn.erase(0, i);
		}
		if (fn.empty()) {
			fn = "headshotKillA.wav";
		} else if (fn.size() < 4 ||
			_stricmp(fn.c_str() + fn.size() - 4, ".wav") != 0) {
			fn += ".wav";
		}

		std::filesystem::path p = std::filesystem::current_path();
		p /= "Data";
		p /= "SKSE";
		p /= "Plugins";
		p /= "HeadshotsKill";
		p /= fn;
		return p;
	}
}  // namespace

void PlayHeadshotKillSound()
{
	auto* settings = Settings::GetSingleton();
	if (!settings->enableHeadshotKillSound) {
		return;
	}
	const float vol = std::clamp(settings->headshotKillSoundVolume, 0.0f, 1.0f);
	if (vol <= 0.0f) {
		return;
	}

	const std::string wavName(settings->headshotKillSoundFile);

	SKSE::GetTaskInterface()->AddTask([vol, wavName]() {
		const auto wavPath = ResolveWavPath(wavName);
		if (!std::filesystem::exists(wavPath)) {
			logger::warn("HeadshotsKill: headshot kill WAV not found: {}", wavPath.string());
			return;
		}
		if (!LoadWAVWithVolume(wavPath, vol, g_headshotKillAudioBuffer)) {
			logger::error("HeadshotsKill: failed to load WAV: {}", wavPath.string());
			return;
		}
		const BOOL ok = PlaySoundA(reinterpret_cast<LPCSTR>(g_headshotKillAudioBuffer.data()),
			nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
		if (!ok) {
			logger::error("HeadshotsKill: PlaySoundA failed (GetLastError={})", GetLastError());
		}
	});
}

void PlayPlayerHelmetKnockoffSound()
{
	auto* settings = Settings::GetSingleton();
	const std::string wavName(settings->playerHelmetKnockoffSoundFile);
	if (wavName.empty()) return;

	static std::vector<std::uint8_t> s_knockoffAudioBuffer;

	SKSE::GetTaskInterface()->AddTask([wavName]() {
		const auto wavPath = ResolveWavPath(wavName);
		if (!std::filesystem::exists(wavPath)) {
			logger::warn("HeadshotsKill: player knockoff WAV not found: {}", wavPath.string());
			return;
		}
		if (!LoadWAVWithVolume(wavPath, 1.0f, s_knockoffAudioBuffer)) {
			logger::error("HeadshotsKill: failed to load player knockoff WAV: {}", wavPath.string());
			return;
		}
		const BOOL ok = PlaySoundA(reinterpret_cast<LPCSTR>(s_knockoffAudioBuffer.data()),
			nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
		if (!ok) {
			logger::error("HeadshotsKill: PlaySoundA failed for knockoff (GetLastError={})", GetLastError());
		}
	});
}
