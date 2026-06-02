#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <SimpleIni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;

namespace logger = SKSE::log;
namespace stl = SKSE::stl;

template <class T>
inline void WriteThunkCall5(std::uintptr_t a_address)
{
	auto& trampoline = SKSE::GetTrampoline();
	T::func = trampoline.write_call<5>(a_address, T::thunk);
}
