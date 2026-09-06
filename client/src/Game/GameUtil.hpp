#pragma once

// Thin helpers over CommonLibF4 for the handful of engine operations F4MP needs.
namespace f4mp::client::game
{
	// The "Player" NPC record in Fallout4.esm. Remote players use a runtime copy of it as the
	// base form of their clone actor.
	inline constexpr RE::TESFormID PLAYER_BASE_FORM_ID = 0x00000007;

	// Where a reference is: exterior worldspace or interior cell.
	struct Space
	{
		std::uint32_t worldspaceId{ 0 };
		std::uint32_t cellId{ 0 };
		bool interior{ false };

		// True when two actors could see each other: same worldspace outdoors, same cell indoors.
		[[nodiscard]] bool SameArea(const Space& a_other) const noexcept
		{
			if (interior != a_other.interior) {
				return false;
			}
			return interior ? cellId == a_other.cellId : worldspaceId == a_other.worldspaceId;
		}
	};

	// The player, or nullptr until it exists and is placed in a cell.
	[[nodiscard]] RE::PlayerCharacter* GetPlayer();

	[[nodiscard]] std::optional<Space> GetSpace(RE::TESObjectREFR* a_ref);
	[[nodiscard]] Space SpaceFromState(const PlayerState& a_state);

	[[nodiscard]] std::string GetPlayerName();

	// Prints to the in-game console (~). Safe to call before the console exists.
	void ConsolePrint(std::string_view a_text);

	[[nodiscard]] RE::NiPoint3 ToNi(const Vec3& a_v);

	// Creates a runtime copy of the player base NPC for one remote player. The copy carries the
	// remote name (so it shows on the crosshair), is a ghost (combat AI ignores it and it takes
	// no damage) and has no aggression, so the engine never fights the network-driven position.
	// Dynamic forms do not survive loading a save: drop the pointer in OnLeaveWorld/OnEnterWorld.
	[[nodiscard]] RE::TESNPC* CreateCloneBase(RE::TESFormID a_source, const std::string& a_name);
	void RenameCloneBase(RE::TESNPC* a_base, const std::string& a_name);

	// Spawns a clone of a_base at the given spot. Returns an empty handle on failure.
	[[nodiscard]] RE::ObjectRefHandle SpawnPlayerClone(RE::TESNPC* a_base, const RE::NiPoint3& a_position, float a_yaw, const Space& a_space);

	// Takes a freshly spawned clone off the AI: "do nothing" package, no combat. Needs the
	// actor's AI process to exist; returns false so the caller retries next frame.
	bool PacifyClone(RE::Actor* a_actor);

	// Pushes sneaking / weapon drawn / sprinting into the clone for every bit that changed.
	void ApplyStateFlags(RE::Actor* a_actor, std::uint8_t a_flags, std::uint8_t a_previous);

	// Disables and deletes the reference (if it still exists) and clears the handle.
	void Despawn(RE::ObjectRefHandle& a_handle);

	// Re-enables player controls, leaves chargen mode, shows the HUD and goes first person.
	// The character-creation menus normally run inside quest scripts that do this afterwards;
	// when they are opened by hand the player can be left frozen with no HUD.
	void RestorePlayerControl();
}
