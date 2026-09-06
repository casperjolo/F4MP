#pragma once

#include "Game/GameUtil.hpp"

namespace f4mp::client
{
	struct Snapshot
	{
		PlayerState state{};
		std::chrono::steady_clock::time_point time{};
	};

	struct RemotePlayer
	{
		PlayerId id{ INVALID_PLAYER_ID };
		std::string name;

		bool hasState{ false };
		Snapshot prev{};
		Snapshot next{};

		// Per-player copy of the player NPC (named, ghost, unaggressive). Dynamic form: it dies
		// with the loaded game, see ForgetActors().
		RE::TESNPC* base{ nullptr };
		bool baseFailed{ false };

		RE::ObjectRefHandle actor{};
		game::Space actorSpace{};
		bool actorDead{ false };
		bool actorPacified{ false };
		std::uint8_t appliedFlags{ kStateNone };
		std::chrono::steady_clock::time_point lastSpawnAttempt{};

		[[nodiscard]] bool IsSpawned() const { return actor.get() != nullptr; }
	};

	// Owns the in-world representation of every other player on the server.
	class RemotePlayers
	{
	public:
		void SetInterpDelay(float a_milliseconds) noexcept { _interpDelayMs = a_milliseconds; }

		// NPC record the clones are copied from. Changing it respawns every clone.
		void SetCloneBase(RE::TESFormID a_formId);
		[[nodiscard]] RE::TESFormID GetCloneBase() const noexcept { return _baseFormId; }

		void Add(PlayerId a_id, std::string a_name);
		void Rename(PlayerId a_id, std::string a_name);
		void Remove(PlayerId a_id);
		void ApplyState(PlayerId a_id, const PlayerState& a_state);

		// Spawns / moves / despawns actors to match the latest snapshots. Call once per frame.
		void Update();

		// Deletes every spawned actor but keeps the player list (used before saving).
		void DespawnAll();

		// Drops actor handles and base forms without touching the engine (the world is being
		// unloaded or was just replaced by a different save).
		void ForgetActors();

		// Removes everyone (disconnect).
		void Clear();

		[[nodiscard]] const RemotePlayer* Find(PlayerId a_id) const;
		[[nodiscard]] std::string NameOf(PlayerId a_id) const;
		[[nodiscard]] std::size_t Count() const noexcept { return _players.size(); }
		[[nodiscard]] const std::unordered_map<PlayerId, RemotePlayer>& All() const noexcept { return _players; }

	private:
		void UpdateOne(RemotePlayer& a_player, const game::Space& a_localSpace, std::chrono::steady_clock::time_point a_now);

		std::unordered_map<PlayerId, RemotePlayer> _players;
		float _interpDelayMs{ 100.0f };
		RE::TESFormID _baseFormId{ game::PLAYER_BASE_FORM_ID };
	};
}
