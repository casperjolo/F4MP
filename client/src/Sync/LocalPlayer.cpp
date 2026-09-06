#include "PCH.hpp"

#include "Sync/LocalPlayer.hpp"

#include "Game/GameUtil.hpp"

namespace f4mp::client
{
	bool LocalPlayer::Sample(PlayerState& a_out) const
	{
		auto* player = game::GetPlayer();
		if (!player) {
			return false;
		}

		const auto space = game::GetSpace(player);
		if (!space) {
			return false;
		}

		const auto position = player->GetPosition();
		a_out.position = Vec3{ position.x, position.y, position.z };
		a_out.yaw = player->GetAngleZ();
		a_out.worldspaceId = space->worldspaceId;
		a_out.cellId = space->cellId;

		std::uint8_t flags = kStateNone;
		if (player->IsSneaking()) {
			flags |= kStateSneaking;
		}
		if (player->weaponState == RE::WEAPON_STATE::kDrawn) {
			flags |= kStateWeaponDrawn;
		}
		if (player->IsDead(true)) {
			flags |= kStateDead;
		}
		if (player->sprinting) {
			flags |= kStateSprinting;
		}
		a_out.flags = flags;

		if (auto* av = RE::ActorValue::GetSingleton(); av && av->health) {
			a_out.health = player->GetActorValue(*av->health);
			a_out.maxHealth = player->GetBaseActorValue(*av->health);
		}

		return true;
	}
}
