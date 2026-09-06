#include "Server.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace f4mp::server
{
	namespace
	{
		constexpr auto CHAT_WINDOW = std::chrono::seconds(5);
		constexpr std::uint32_t CHAT_BURST = 6;

		bool Finite(const Vec3& a_v) noexcept
		{
			return std::isfinite(a_v.x) && std::isfinite(a_v.y) && std::isfinite(a_v.z);
		}

		// Rejects states a real client can never produce (NaN/inf from a broken or hostile client).
		bool Plausible(const PlayerState& a_state) noexcept
		{
			return Finite(a_state.position) &&
			       std::isfinite(a_state.yaw) &&
			       std::isfinite(a_state.health) &&
			       std::isfinite(a_state.maxHealth);
		}

		std::string PeerAddress(const ENetPeer* a_peer)
		{
			char host[64]{};
			if (enet_address_get_host_ip(&a_peer->address, host, sizeof(host)) != 0) {
				return "?";
			}
			return fmt::format("{}:{}", host, a_peer->address.port);
		}

		std::vector<std::string> Split(const std::string& a_line)
		{
			std::vector<std::string> out;
			std::istringstream stream(a_line);
			std::string token;
			while (stream >> token) {
				out.push_back(token);
			}
			return out;
		}
	}

	Server::Server(Config config)
		: _config(std::move(config))
	{
	}

	Server::~Server()
	{
		Stop();
	}

	bool Server::Start()
	{
		if (enet_initialize() != 0) {
			spdlog::critical("enet_initialize() failed");
			return false;
		}

		ENetAddress address{};
		address.host = ENET_HOST_ANY;
		address.port = _config.port;

		_host = enet_host_create(&address, _config.maxPlayers, static_cast<std::size_t>(Channel::kCount), 0, 0);
		if (!_host) {
			spdlog::critical("Failed to bind UDP port {}", _config.port);
			enet_deinitialize();
			return false;
		}

		spdlog::info("\"{}\" listening on UDP {} (max {} players, protocol v{}, tick {} Hz)",
			_config.name, _config.port, _config.maxPlayers, PROTOCOL_VERSION, _config.tickRate);
		return true;
	}

	void Server::Stop()
	{
		if (!_host) {
			return;
		}

		for (auto& [peer, player] : _players) {
			enet_peer_disconnect_now(peer, 0);
		}
		_players.clear();

		enet_host_destroy(_host);
		_host = nullptr;
		enet_deinitialize();
		spdlog::info("Server stopped");
	}

	void Server::Service(std::uint32_t a_timeoutMs)
	{
		if (!_host) {
			return;
		}

		ENetEvent event{};
		auto waited = false;
		while (enet_host_service(_host, &event, waited ? 0 : a_timeoutMs) > 0) {
			waited = true;
			switch (event.type) {
			case ENET_EVENT_TYPE_CONNECT:
				HandleConnect(event.peer);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				HandleDisconnect(event.peer);
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				HandlePacket(event.peer, std::span<const std::uint8_t>(event.packet->data, event.packet->dataLength));
				enet_packet_destroy(event.packet);
				break;
			default:
				break;
			}
		}
	}

	// ---- connection lifecycle ---------------------------------------------

	void Server::HandleConnect(ENetPeer* a_peer)
	{
		auto player = std::make_unique<Player>();
		player->peer = a_peer;
		player->joinedAt = std::chrono::steady_clock::now();
		spdlog::info("Incoming connection from {}", PeerAddress(a_peer));
		_players.emplace(a_peer, std::move(player));
	}

	void Server::HandleDisconnect(ENetPeer* a_peer)
	{
		const auto it = _players.find(a_peer);
		if (it == _players.end()) {
			return;
		}

		auto player = std::move(it->second);
		_players.erase(it);

		if (player->handshaked) {
			spdlog::info("{} (#{}) left", player->name, player->id);
			Broadcast(Channel::kReliable, Encode(PlayerLeftMsg{ player->id }));
			ServerChat(fmt::format("{} left the game", player->name));
		} else {
			spdlog::info("{} disconnected before handshake", PeerAddress(a_peer));
		}
	}

	void Server::HandlePacket(ENetPeer* a_peer, std::span<const std::uint8_t> a_bytes)
	{
		auto* player = FindPlayer(a_peer);
		if (!player) {
			return;
		}

		Reader reader(a_bytes);
		MsgId id{};
		if (!ReadHeader(reader, id)) {
			return;
		}

		if (!player->handshaked && id != MsgId::kHello) {
			Kick(*player, "Expected handshake");
			return;
		}

		switch (id) {
		case MsgId::kHello:
			HandleHello(*player, reader);
			break;
		case MsgId::kPlayerState:
			HandlePlayerState(*player, reader);
			break;
		case MsgId::kChat:
			HandleChat(*player, reader);
			break;
		default:
			spdlog::warn("{} sent unknown message id {}", PeerAddress(a_peer), static_cast<int>(id));
			break;
		}
	}

	// ---- message handlers ---------------------------------------------------

	void Server::HandleHello(Player& a_player, Reader& a_reader)
	{
		HelloMsg hello;
		if (!hello.Read(a_reader)) {
			Kick(a_player, "Malformed hello");
			return;
		}

		if (hello.protocolVersion != PROTOCOL_VERSION) {
			Kick(a_player, fmt::format("Protocol mismatch (server v{}, client v{})", PROTOCOL_VERSION, hello.protocolVersion));
			return;
		}

		if (a_player.handshaked) {
			return;
		}

		a_player.id = _nextId++;
		a_player.name = SanitizeName(hello.name);
		a_player.gameVersion = hello.gameVersion;
		a_player.handshaked = true;

		spdlog::info("{} joined as #{} from {} (game 0x{:08X})",
			a_player.name, a_player.id, PeerAddress(a_player.peer), a_player.gameVersion);

		WelcomeMsg welcome;
		welcome.id = a_player.id;
		welcome.tickRate = static_cast<std::uint8_t>(std::min<std::uint32_t>(_config.tickRate, 255));
		welcome.serverName = _config.name;
		welcome.motd = _config.motd;
		Send(a_player.peer, Channel::kReliable, Encode(welcome));

		// Tell the newcomer about everyone already here (and where they are).
		for (const auto& [peer, other] : _players) {
			if (!other->handshaked || other.get() == &a_player) {
				continue;
			}
			Send(a_player.peer, Channel::kReliable, Encode(PlayerJoinedMsg{ other->id, other->name }));
			if (other->hasState) {
				Send(a_player.peer, Channel::kReliable, Encode(PlayerStateUpdateMsg{ other->id, other->state }));
			}
		}

		Broadcast(Channel::kReliable, Encode(PlayerJoinedMsg{ a_player.id, a_player.name }), &a_player);
		ServerChat(fmt::format("{} joined the game", a_player.name));
	}

	void Server::HandlePlayerState(Player& a_player, Reader& a_reader)
	{
		PlayerStateMsg msg;
		if (!msg.Read(a_reader) || !Plausible(msg.state)) {
			return;
		}

		a_player.state = msg.state;
		a_player.hasState = true;

		// Relay straight away; clients interpolate between updates.
		Broadcast(Channel::kUnreliable, Encode(PlayerStateUpdateMsg{ a_player.id, a_player.state }), &a_player);
	}

	void Server::HandleChat(Player& a_player, Reader& a_reader)
	{
		ChatMsg msg;
		if (!msg.Read(a_reader) || msg.text.empty()) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - a_player.chatWindowStart >= CHAT_WINDOW) {
			a_player.chatWindowStart = now;
			a_player.chatWindowCount = 0;
		}
		if (++a_player.chatWindowCount > CHAT_BURST) {
			if (a_player.chatWindowCount == CHAT_BURST + 1) {
				Send(a_player.peer, Channel::kReliable, Encode(ChatBroadcastMsg{ SERVER_ID, "Slow down." }));
			}
			return;
		}

		if (msg.text.size() > MAX_CHAT_BYTES) {
			msg.text.resize(MAX_CHAT_BYTES);
		}
		std::erase_if(msg.text, [](unsigned char c) { return c < 0x20 || c == 0x7F; });
		if (msg.text.empty()) {
			return;
		}

		spdlog::info("[chat] {}: {}", a_player.name, msg.text);
		Broadcast(Channel::kReliable, Encode(ChatBroadcastMsg{ a_player.id, msg.text }));
	}

	// ---- sending --------------------------------------------------------------

	void Server::Send(ENetPeer* a_peer, Channel a_channel, const std::vector<std::uint8_t>& a_bytes)
	{
		const enet_uint32 flags = a_channel == Channel::kReliable ? ENET_PACKET_FLAG_RELIABLE : 0u;
		auto* packet = enet_packet_create(a_bytes.data(), a_bytes.size(), flags);
		if (packet) {
			enet_peer_send(a_peer, static_cast<enet_uint8>(a_channel), packet);
		}
	}

	void Server::Broadcast(Channel a_channel, const std::vector<std::uint8_t>& a_bytes, const Player* a_except)
	{
		for (const auto& [peer, player] : _players) {
			if (!player->handshaked || player.get() == a_except) {
				continue;
			}
			Send(peer, a_channel, a_bytes);
		}
	}

	void Server::ServerChat(const std::string& a_text)
	{
		Broadcast(Channel::kReliable, Encode(ChatBroadcastMsg{ SERVER_ID, a_text }));
	}

	void Server::Kick(Player& a_player, const std::string& a_reason)
	{
		spdlog::info("Kicking {} (#{}): {}",
			a_player.name.empty() ? PeerAddress(a_player.peer) : a_player.name, a_player.id, a_reason);
		Send(a_player.peer, Channel::kReliable, Encode(RejectMsg{ a_reason }));
		enet_host_flush(_host);
		enet_peer_disconnect_later(a_player.peer, 0);
	}

	Player* Server::FindPlayer(ENetPeer* a_peer)
	{
		const auto it = _players.find(a_peer);
		return it == _players.end() ? nullptr : it->second.get();
	}

	Player* Server::FindPlayer(PlayerId a_id)
	{
		for (auto& [peer, player] : _players) {
			if (player->handshaked && player->id == a_id) {
				return player.get();
			}
		}
		return nullptr;
	}

	// ---- console --------------------------------------------------------------

	void Server::ExecuteCommand(const std::string& a_line)
	{
		const auto args = Split(a_line);
		if (args.empty()) {
			return;
		}

		const auto& cmd = args[0];
		if (cmd == "help") {
			spdlog::info("Commands: help, list, say <text>, kick <id> [reason], stop");
		} else if (cmd == "list") {
			spdlog::info("{} connection(s):", _players.size());
			for (const auto& [peer, player] : _players) {
				if (!player->handshaked) {
					spdlog::info("  (handshaking) {}", PeerAddress(peer));
					continue;
				}
				const auto& s = player->state;
				spdlog::info("  #{} {} @ ({:.0f}, {:.0f}, {:.0f}) ws=0x{:08X} cell=0x{:08X} hp={:.0f}/{:.0f} ping={}ms",
					player->id, player->name, s.position.x, s.position.y, s.position.z,
					s.worldspaceId, s.cellId, s.health, s.maxHealth, peer->roundTripTime);
			}
		} else if (cmd == "say") {
			const auto pos = a_line.find(' ');
			if (pos != std::string::npos) {
				const auto text = a_line.substr(pos + 1);
				spdlog::info("[chat] SERVER: {}", text);
				ServerChat(text);
			}
		} else if (cmd == "kick") {
			if (args.size() < 2) {
				spdlog::warn("usage: kick <id> [reason]");
				return;
			}
			PlayerId id{};
			try {
				id = static_cast<PlayerId>(std::stoul(args[1]));
			} catch (...) {
				spdlog::warn("usage: kick <id> [reason]");
				return;
			}
			auto* player = FindPlayer(id);
			if (!player) {
				spdlog::warn("No player with id {}", id);
				return;
			}
			std::string reason = "Kicked by server";
			if (args.size() > 2) {
				reason = a_line.substr(a_line.find(args[1]) + args[1].size() + 1);
			}
			Kick(*player, reason);
		} else {
			spdlog::warn("Unknown command \"{}\" (try help)", cmd);
		}
	}
}
