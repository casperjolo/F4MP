#pragma once

#include "Game/GameUtil.hpp"

#include "f4mp/Interp.hpp"

namespace f4mp::client
{
	struct RemotePlayer
	{
		PlayerId id{ INVALID_PLAYER_ID };
		std::string name;

		std::vector<TimedState> snapshots; // oldest first, bounded

		// Per-player copy of the player NPC (only with the duplicate experiment). Dynamic form:
		// it dies with the loaded game, see ForgetActors().
		RE::TESNPC* base{ nullptr };
		bool baseFailed{ false };

		RE::ObjectRefHandle actor{};
		game::Space actorSpace{};
		bool actorDead{ false };
		bool actorPacified{ false };
		bool locomotionInit{ false };
		std::uint8_t appliedFlags{ kStateNone };
		std::chrono::steady_clock::time_point lastSpawnAttempt{};

		// Locomotion derived from the rendered motion.
		bool hasLastRender{ false };
		RE::NiPoint3 lastRenderPos{};
		std::chrono::steady_clock::time_point lastRenderTime{};
		float speed{ 0.0f }; // smoothed, units/s

		[[nodiscard]] bool HasState() const noexcept { return !snapshots.empty(); }
		[[nodiscard]] const PlayerState& Latest() const { return snapshots.back().state; }
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

		// Experiment switches (f4mp sync / pacify / ghost / duplicate). All but sync respawn the clones.
		void SetDrive(bool a_drive) noexcept { _drive = a_drive; }
		void SetPacify(bool a_pacify);
		void SetGhost(bool a_ghost);
		void SetDuplicate(bool a_duplicate);
		void SetMove(bool a_move) noexcept { _move = a_move; }
		void Respawn();
		[[nodiscard]] bool GetDrive() const noexcept { return _drive; }
		[[nodiscard]] bool GetPacify() const noexcept { return _pacify; }
		[[nodiscard]] bool GetGhost() const noexcept { return _ghost; }
		[[nodiscard]] bool GetDuplicate() const noexcept { return _duplicate; }
		[[nodiscard]] bool GetMove() const noexcept { return _move; }

		void Add(PlayerId a_id, std::string a_name);
		void Rename(PlayerId a_id, std::string a_name);
		void Remove(PlayerId a_id);
		void ApplyState(PlayerId a_id, const PlayerState& a_state, std::uint32_t a_serverMs);

		// Spawns / moves / despawns actors to match the snapshots. Call once per frame.
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
		[[nodiscard]] double GetClockOffsetMs() const noexcept { return _offsetMs; }

	private:
		using Clock = std::chrono::steady_clock;

		void UpdateOne(RemotePlayer& a_player, const game::Space& a_localSpace, Clock::time_point a_now, double a_renderMs);

		[[nodiscard]] static double LocalMs() noexcept;
		void NoteServerTime(std::int64_t a_serverMs);

		std::unordered_map<PlayerId, RemotePlayer> _players;
		float _interpDelayMs{ 100.0f };
		RE::TESFormID _baseFormId{ game::PLAYER_BASE_FORM_ID };
		bool _drive{ true };      // write position / heading / flags every frame
		bool _pacify{ true };     // do-nothing package once the AI process exists
		bool _ghost{ true };      // ghost (base flag when duplicating, per actor otherwise)
		bool _duplicate{ false }; // per-player runtime copy of the base record (copies T-pose, see ARCHITECTURE)
		bool _move{ false };      // experiment: Actor::Move instead of SetPosition warps

		// localMs - serverMs, min-filtered so it tracks the fastest packets (transit + clock skew).
		bool _hasOffset{ false };
		double _offsetMs{ 0.0 };
		ServerClock _serverClock; // u32 wire stamps -> monotonic timeline
	};
}
