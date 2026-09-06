#pragma once

#include <cstdint>

namespace f4mp
{
	using PlayerId = std::uint32_t;

	inline constexpr PlayerId SERVER_ID = 0;
	inline constexpr PlayerId INVALID_PLAYER_ID = 0xFFFFFFFFu;

	struct Vec3
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	// Bit flags describing an actor's coarse animation / life state.
	enum StateFlags : std::uint8_t
	{
		kStateNone = 0,
		kStateSneaking = 1 << 0,
		kStateWeaponDrawn = 1 << 1,
		kStateDead = 1 << 2,
		kStateSprinting = 1 << 3,
	};

	// Everything a client needs to place another player's character in its world.
	struct PlayerState
	{
		Vec3 position{};
		float yaw{ 0.0f };               // radians, game Z angle
		std::uint32_t worldspaceId{ 0 }; // 0 while inside an interior cell
		std::uint32_t cellId{ 0 };
		std::uint8_t flags{ kStateNone };
		float health{ 0.0f };
		float maxHealth{ 0.0f };
	};
}
