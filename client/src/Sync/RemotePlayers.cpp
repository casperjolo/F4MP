#include "PCH.hpp"

#include "Sync/RemotePlayers.hpp"

namespace f4mp::client
{
	namespace
	{
		constexpr auto SPAWN_RETRY_INTERVAL = std::chrono::seconds(1);
		constexpr std::size_t MAX_SNAPSHOTS = 64;
		constexpr double SNAPSHOT_KEEP_MS = 1500.0;   // history kept behind the render time
		constexpr double MAX_EXTRAPOLATE_MS = 150.0;  // beyond the newest snapshot
		constexpr double MAX_VELOCITY_GAP_MS = 400.0; // pairs further apart carry no useful velocity

		float Lerp(float a_a, float a_b, float a_t) noexcept
		{
			return a_a + (a_b - a_a) * a_t;
		}

		// Interpolates along the shortest arc so yaw does not spin through 360 degrees.
		float LerpAngle(float a_a, float a_b, float a_t) noexcept
		{
			constexpr auto twoPi = 2.0f * std::numbers::pi_v<float>;
			auto delta = std::fmod(a_b - a_a, twoPi);
			if (delta > std::numbers::pi_v<float>) {
				delta -= twoPi;
			} else if (delta < -std::numbers::pi_v<float>) {
				delta += twoPi;
			}
			return a_a + delta * a_t;
		}

		PlayerState Blend(const PlayerState& a_a, const PlayerState& a_b, float a_t)
		{
			PlayerState out = a_t < 0.5f ? a_a : a_b; // discrete fields follow the nearer snapshot
			out.position.x = Lerp(a_a.position.x, a_b.position.x, a_t);
			out.position.y = Lerp(a_a.position.y, a_b.position.y, a_t);
			out.position.z = Lerp(a_a.position.z, a_b.position.z, a_t);
			out.yaw = LerpAngle(a_a.yaw, a_b.yaw, a_t);
			out.health = Lerp(a_a.health, a_b.health, a_t);
			return out;
		}
	}

	// ---- bookkeeping --------------------------------------------------------------

	void RemotePlayers::Add(PlayerId a_id, std::string a_name)
	{
		auto& player = _players[a_id];
		player.id = a_id;
		player.name = std::move(a_name);
		// State can arrive before the join message and create the entry with a placeholder name.
		game::RenameCloneBase(player.base, player.name);
		REX::LogInformation("Remote player #{} \"{}\" added"sv, a_id, player.name);
	}

	void RemotePlayers::Rename(PlayerId a_id, std::string a_name)
	{
		const auto it = _players.find(a_id);
		if (it == _players.end()) {
			Add(a_id, std::move(a_name));
			return;
		}
		it->second.name = std::move(a_name);
		game::RenameCloneBase(it->second.base, it->second.name);
		if (auto ref = it->second.actor.get()) {
			game::ApplyCloneIdentity(ref.get(), it->second.name, false);
		}
		REX::LogInformation("Remote player #{} renamed to \"{}\""sv, a_id, it->second.name);
	}

	void RemotePlayers::Remove(PlayerId a_id)
	{
		const auto it = _players.find(a_id);
		if (it == _players.end()) {
			return;
		}
		game::Despawn(it->second.actor);
		REX::LogInformation("Remote player #{} \"{}\" removed"sv, a_id, it->second.name);
		_players.erase(it);
	}

	void RemotePlayers::ApplyState(PlayerId a_id, const PlayerState& a_state, std::uint32_t a_serverMs)
	{
		auto it = _players.find(a_id);
		if (it == _players.end()) {
			// State can arrive before the join message on the unreliable channel; keep it anyway.
			Add(a_id, "#" + std::to_string(a_id));
			it = _players.find(a_id);
		}

		NoteServerTime(a_serverMs);

		auto& snapshots = it->second.snapshots;
		// Sequenced channel: a stale packet is dropped by ENet, but be safe about ordering.
		if (!snapshots.empty() && static_cast<std::int32_t>(a_serverMs - snapshots.back().serverMs) < 0) {
			return;
		}
		snapshots.push_back(Snapshot{ a_state, a_serverMs });
		while (snapshots.size() > MAX_SNAPSHOTS) {
			snapshots.pop_front();
		}
	}

	// ---- clock ------------------------------------------------------------------------

	double RemotePlayers::LocalMs() noexcept
	{
		return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
	}

	void RemotePlayers::NoteServerTime(std::uint32_t a_serverMs)
	{
		const double sample = LocalMs() - static_cast<double>(a_serverMs);
		if (!_hasOffset) {
			_hasOffset = true;
			_offsetMs = sample;
		} else if (sample < _offsetMs) {
			_offsetMs = sample; // a faster packet: better estimate of the minimum transit time
		} else {
			_offsetMs += (sample - _offsetMs) * 0.02; // slow drift so clock skew cannot pin us
		}
	}

	// ---- per frame ------------------------------------------------------------------

	void RemotePlayers::Update()
	{
		auto* localPlayer = game::GetPlayer();
		const auto localSpace = game::GetSpace(localPlayer);
		if (!localSpace) {
			return;
		}

		const auto now = Clock::now();
		// Render slightly in the past (server time) so there is normally a snapshot ahead of us.
		const double renderMs = LocalMs() - _offsetMs - static_cast<double>(_interpDelayMs);
		for (auto& [id, player] : _players) {
			UpdateOne(player, *localSpace, now, renderMs);
		}
	}

	PlayerState RemotePlayers::Sample(const RemotePlayer& a_player, double a_renderMs)
	{
		const auto& s = a_player.snapshots;
		if (s.size() == 1 || a_renderMs <= static_cast<double>(s.front().serverMs)) {
			return s.front().state;
		}

		const auto& last = s.back();
		if (a_renderMs >= static_cast<double>(last.serverMs)) {
			// Past the newest snapshot: extrapolate a little from the last pair, then hold.
			const auto& prev = s[s.size() - 2];
			const double gap = static_cast<double>(last.serverMs - prev.serverMs);
			const double ahead = std::min(a_renderMs - static_cast<double>(last.serverMs), MAX_EXTRAPOLATE_MS);
			if (gap <= 0.0 || gap > MAX_VELOCITY_GAP_MS || ahead <= 0.0) {
				return last.state;
			}
			const float t = static_cast<float>(ahead / gap);
			PlayerState out = last.state;
			out.position.x += (last.state.position.x - prev.state.position.x) * t;
			out.position.y += (last.state.position.y - prev.state.position.y) * t;
			out.position.z += (last.state.position.z - prev.state.position.z) * t;
			return out;
		}

		// Find the pair bracketing the render time.
		for (std::size_t i = 0; i + 1 < s.size(); ++i) {
			const auto& a = s[i];
			const auto& b = s[i + 1];
			if (a_renderMs < static_cast<double>(a.serverMs) || a_renderMs > static_cast<double>(b.serverMs)) {
				continue;
			}
			const double span = static_cast<double>(b.serverMs - a.serverMs);
			const float t = span > 0.0 ? static_cast<float>((a_renderMs - static_cast<double>(a.serverMs)) / span) : 1.0f;
			return Blend(a.state, b.state, std::clamp(t, 0.0f, 1.0f));
		}
		return last.state;
	}

	void RemotePlayers::UpdateOne(RemotePlayer& a_player, const game::Space& a_localSpace, Clock::time_point a_now, double a_renderMs)
	{
		if (!a_player.HasState()) {
			return;
		}

		// Forget history well behind the render time (keep two so velocity is still available).
		auto& snapshots = a_player.snapshots;
		while (snapshots.size() > 2 && static_cast<double>(snapshots[1].serverMs) + SNAPSHOT_KEEP_MS < a_renderMs) {
			snapshots.pop_front();
		}

		const auto& latest = a_player.Latest();
		const auto targetSpace = game::SpaceFromState(latest);
		const auto visible = targetSpace.SameArea(a_localSpace);

		auto ref = a_player.actor.get();
		RE::Actor* actor = ref ? ref->As<RE::Actor>() : nullptr;

		// Never, under any circumstance, drive the local player as if it were a clone.
		if (actor && actor == RE::PlayerCharacter::GetSingleton()) {
			REX::LogError("Remote #{} resolved to the local player; dropping the handle"sv, a_player.id);
			a_player.actor.reset();
			actor = nullptr;
		}

		// Tear down the actor when the remote player is somewhere we cannot see.
		if (actor && (!visible || !a_player.actorSpace.SameArea(targetSpace))) {
			game::Despawn(a_player.actor);
			actor = nullptr;
		}

		if (!actor) {
			if (!visible || a_now - a_player.lastSpawnAttempt < SPAWN_RETRY_INTERVAL) {
				return;
			}
			a_player.lastSpawnAttempt = a_now;

			RE::TESNPC* base = nullptr;
			if (_duplicate) {
				if (!a_player.base && !a_player.baseFailed) {
					a_player.base = game::CreateCloneBase(_baseFormId, a_player.name, _ghost);
					a_player.baseFailed = a_player.base == nullptr;
				}
				base = a_player.base;
			} else {
				base = RE::TESForm::FindFormByID<RE::TESNPC>(_baseFormId);
				if (!base && !a_player.baseFailed) {
					REX::LogError("Clone base NPC 0x{:08X} does not exist"sv, _baseFormId);
					a_player.baseFailed = true;
				}
			}
			if (!base) {
				return;
			}

			a_player.actor = game::SpawnPlayerClone(base, game::ToNi(latest.position), latest.yaw, targetSpace);
			ref = a_player.actor.get();
			actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				REX::LogWarning("Failed to spawn actor for #{} \"{}\""sv, a_player.id, a_player.name);
				return;
			}
			if (!_duplicate) {
				game::ApplyCloneIdentity(ref.get(), a_player.name, _ghost);
			}

			a_player.actorSpace = targetSpace;
			a_player.actorDead = false;
			a_player.actorPacified = false;
			a_player.locomotionInit = false;
			a_player.hasLastRender = false;
			a_player.speed = 0.0f;
			a_player.appliedFlags = kStateNone;
			REX::LogInformation("Spawned actor 0x{:08X} for #{} \"{}\""sv, actor->GetFormID(), a_player.id, a_player.name);
		}

		// The AI process appears a frame or so after spawning; keep trying until it is there.
		if (_pacify && !a_player.actorPacified) {
			a_player.actorPacified = game::PacifyClone(actor);
		}

		// Placed actors start fully transparent and rely on the engine's fade-in, which never
		// runs for a clone that is pacified and warped every frame. Keep it opaque.
		if (actor->GetAlpha() < 1.0f) {
			actor->SetAlpha(1.0f);
		}

		if (!_drive) {
			return; // experiment: leave the actor entirely to the engine
		}

		const auto sample = Sample(a_player, a_renderMs);
		const auto position = game::ToNi(sample.position);
		const float frameDt = a_player.hasLastRender ? std::chrono::duration<float>(a_now - a_player.lastRenderTime).count() : 0.0f;
		if (_move && frameDt > 0.0f) {
			game::DriveCloneByMove(actor, position, sample.yaw, std::min(frameDt, 0.1f));
		} else {
			game::DriveClone(actor, position, sample.yaw);
		}

		// Speed of the rendered motion, smoothed, drives the walk / run animation.
		if (a_player.hasLastRender) {
			const float dt = frameDt;
			if (dt > 0.0f) {
				const float dx = position.x - a_player.lastRenderPos.x;
				const float dy = position.y - a_player.lastRenderPos.y;
				const float instant = std::sqrt(dx * dx + dy * dy) / dt;
				a_player.speed += (instant - a_player.speed) * std::clamp(dt * 12.0f, 0.0f, 1.0f);
			}
		}
		a_player.lastRenderPos = position;
		a_player.lastRenderTime = a_now;
		a_player.hasLastRender = true;

		const auto dead = (latest.flags & kStateDead) != 0;
		if (dead != a_player.actorDead) {
			if (dead) {
				actor->KillImpl(nullptr, 0.0f, false, false);
			} else {
				actor->Resurrect(false, true);
			}
			a_player.actorDead = dead;
		}

		// Sneak / weapon / sprint / locomotion only matter while alive; a corpse keeps whatever it had.
		if (!dead) {
			const auto flags = static_cast<std::uint8_t>(latest.flags & ~kStateDead);
			game::ApplyStateFlags(actor, flags, a_player.appliedFlags);
			a_player.appliedFlags = flags;
			game::SetLocomotion(actor, a_player.speed, (flags & kStateSprinting) != 0, !a_player.locomotionInit);
			a_player.locomotionInit = true;
		}
	}

	// ---- lifecycle ----------------------------------------------------------------------

	void RemotePlayers::DespawnAll()
	{
		for (auto& [id, player] : _players) {
			game::Despawn(player.actor);
		}
	}

	void RemotePlayers::Respawn()
	{
		DespawnAll();
		for (auto& [id, player] : _players) {
			player.base = nullptr; // the old dynamic copy is simply left behind
			player.baseFailed = false;
			player.actorDead = false;
			player.actorPacified = false;
			player.locomotionInit = false;
			player.hasLastRender = false;
			player.speed = 0.0f;
			player.appliedFlags = kStateNone;
			player.lastSpawnAttempt = {};
		}
		REX::LogInformation("Clones respawn (base 0x{:08X}, drive {}, pacify {}, ghost {}, duplicate {})"sv, _baseFormId, _drive, _pacify, _ghost, _duplicate);
	}

	void RemotePlayers::SetCloneBase(RE::TESFormID a_formId)
	{
		_baseFormId = a_formId;
		Respawn();
	}

	void RemotePlayers::SetPacify(bool a_pacify)
	{
		_pacify = a_pacify;
		Respawn();
	}

	void RemotePlayers::SetGhost(bool a_ghost)
	{
		_ghost = a_ghost;
		Respawn();
	}

	void RemotePlayers::SetDuplicate(bool a_duplicate)
	{
		_duplicate = a_duplicate;
		Respawn();
	}

	void RemotePlayers::ForgetActors()
	{
		for (auto& [id, player] : _players) {
			player.actor.reset();
			player.actorDead = false;
			player.actorPacified = false;
			player.locomotionInit = false;
			player.hasLastRender = false;
			player.speed = 0.0f;
			player.appliedFlags = kStateNone;
			player.base = nullptr; // dynamic form, gone with the previous game
			player.baseFailed = false;
		}
	}

	void RemotePlayers::Clear()
	{
		DespawnAll();
		_players.clear();
		_hasOffset = false;
	}

	const RemotePlayer* RemotePlayers::Find(PlayerId a_id) const
	{
		const auto it = _players.find(a_id);
		return it == _players.end() ? nullptr : &it->second;
	}

	std::string RemotePlayers::NameOf(PlayerId a_id) const
	{
		const auto* player = Find(a_id);
		return player ? player->name : "#" + std::to_string(a_id);
	}
}
