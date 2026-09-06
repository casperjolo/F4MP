#include "PCH.hpp"

#include "Sync/RemotePlayers.hpp"

namespace f4mp::client
{
	namespace
	{
		constexpr auto SPAWN_RETRY_INTERVAL = std::chrono::seconds(1);

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
	}

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

	void RemotePlayers::ApplyState(PlayerId a_id, const PlayerState& a_state)
	{
		auto it = _players.find(a_id);
		if (it == _players.end()) {
			// State can arrive before the join message on the unreliable channel; keep it anyway.
			Add(a_id, "#" + std::to_string(a_id));
			it = _players.find(a_id);
		}

		auto& player = it->second;
		const auto now = std::chrono::steady_clock::now();
		if (!player.hasState) {
			player.prev = Snapshot{ a_state, now };
			player.next = player.prev;
			player.hasState = true;
		} else {
			player.prev = player.next;
			player.next = Snapshot{ a_state, now };
		}
	}

	void RemotePlayers::Update()
	{
		auto* localPlayer = game::GetPlayer();
		const auto localSpace = game::GetSpace(localPlayer);
		if (!localSpace) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		for (auto& [id, player] : _players) {
			UpdateOne(player, *localSpace, now);
		}
	}

	void RemotePlayers::UpdateOne(RemotePlayer& a_player, const game::Space& a_localSpace, std::chrono::steady_clock::time_point a_now)
	{
		if (!a_player.hasState) {
			return;
		}

		const auto targetSpace = game::SpaceFromState(a_player.next.state);
		const auto visible = targetSpace.SameArea(a_localSpace);

		auto ref = a_player.actor.get();
		RE::Actor* actor = ref ? ref->As<RE::Actor>() : nullptr;

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

			a_player.actor = game::SpawnPlayerClone(base, game::ToNi(a_player.next.state.position), a_player.next.state.yaw, targetSpace);
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

		// Render the remote player slightly in the past so there is always a snapshot to move towards.
		const auto delay = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<float, std::milli>(_interpDelayMs));
		const auto renderTime = a_now - delay;

		float t = 1.0f;
		if (a_player.next.time > a_player.prev.time) {
			const auto span = std::chrono::duration<float>(a_player.next.time - a_player.prev.time).count();
			const auto elapsed = std::chrono::duration<float>(renderTime - a_player.prev.time).count();
			t = std::clamp(elapsed / span, 0.0f, 1.0f);
		}

		const auto& a = a_player.prev.state;
		const auto& b = a_player.next.state;

		const RE::NiPoint3 position(
			Lerp(a.position.x, b.position.x, t),
			Lerp(a.position.y, b.position.y, t),
			Lerp(a.position.z, b.position.z, t));
		const auto yaw = LerpAngle(a.yaw, b.yaw, t);

		actor->SetPosition(position, true);
		actor->SetHeading(yaw);

		const auto dead = (b.flags & kStateDead) != 0;
		if (dead != a_player.actorDead) {
			if (dead) {
				actor->KillImpl(nullptr, 0.0f, false, false);
			} else {
				actor->Resurrect(false, true);
			}
			a_player.actorDead = dead;
		}

		// Sneak / weapon / sprint only matter while alive; a corpse keeps whatever it had.
		if (!dead) {
			const auto flags = static_cast<std::uint8_t>(b.flags & ~kStateDead);
			game::ApplyStateFlags(actor, flags, a_player.appliedFlags);
			a_player.appliedFlags = flags;
		}
	}

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
			player.appliedFlags = kStateNone;
			player.base = nullptr; // dynamic form, gone with the previous game
			player.baseFailed = false;
		}
	}

	void RemotePlayers::Clear()
	{
		DespawnAll();
		_players.clear();
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
