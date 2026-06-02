#pragma once

#include "PCH.h"

enum class RelocId : std::uint32_t
{
	ProjectileImpact,
	HandleProjectileAttack,
};

namespace Reloc
{
	inline bool IsPreAE()
	{
		return REL::Module::get().version() <= SKSE::RUNTIME_SSE_1_5_97;
	}

	inline REL::Relocation<std::uintptr_t> Address(RelocId a_id)
	{
		const bool se = IsPreAE();
		switch (a_id) {
		case RelocId::ProjectileImpact:
			return se ? REL::Relocation<std::uintptr_t>(REL::ID(43013), 0x3E3) :
			            REL::Relocation<std::uintptr_t>(REL::ID(44204), 0x3D4);
		case RelocId::HandleProjectileAttack:
			return se ? REL::Relocation<std::uintptr_t>(REL::ID(36016), 0x926) :
			            REL::Relocation<std::uintptr_t>(REL::ID(36991), 0xA3D);
		}
		return {};
	}
}
