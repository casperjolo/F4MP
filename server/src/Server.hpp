#pragma once

#include "Config.hpp"

#include "f4mp/Protocol.hpp"

#include <enet/enet.h>

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace f4mp::server
{
	struct Player
	{
		PlayerId id{ INVALID_PLAYER_ID };
		std::string name;
		ENetPeer* peer{ nullptr };
		PlayerState state{};
		bool handshaked{ false };
		bool hasState{ false };
		std::uint32_t gameVersion{ 0 };
		std::chrono::steady_clock::time_point joinedAt{};

		// Chat flood control: at most CHAT_BURST messages per CHAT_WINDOW.
		std::chrono::steady_clock::time_point chatWindowStart{};
		std::uint32_t chatWindowCount{ 0 };
	};

	class Server
	{
	public:
		explicit Server(Config config);
		~Server();

		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		[[nodiscard]] bool Start();
		void Stop();

		// Services the network for up to a_timeoutMs milliseconds. Call from the main loop.
		void Service(std::uint32_t a_timeoutMs);

		// Console commands (help, list, say, kick).
		void ExecuteCommand(const std::string& a_line);

		[[nodiscard]] const Config& GetConfig() const noexcept { return _config; }
		[[nodiscard]] std::size_t GetPlayerCount() const noexcept { return _players.size(); }

	private:
		void HandleConnect(ENetPeer* a_peer);
		void HandleDisconnect(ENetPeer* a_peer);
		void HandlePacket(ENetPeer* a_peer, std::span<const std::uint8_t> a_bytes);

		void HandleHello(Player& a_player, Reader& a_reader);
		void HandlePlayerState(Player& a_player, Reader& a_reader);
		void HandleChat(Player& a_player, Reader& a_reader);

		void Send(ENetPeer* a_peer, Channel a_channel, const std::vector<std::uint8_t>& a_bytes);
		void Broadcast(Channel a_channel, const std::vector<std::uint8_t>& a_bytes, const Player* a_except = nullptr);
		void ServerChat(const std::string& a_text);
		void Kick(Player& a_player, const std::string& a_reason);

		[[nodiscard]] Player* FindPlayer(ENetPeer* a_peer);
		[[nodiscard]] Player* FindPlayer(PlayerId a_id);

		Config _config;
		ENetHost* _host{ nullptr };
		std::unordered_map<ENetPeer*, std::unique_ptr<Player>> _players;
		PlayerId _nextId{ 1 };
	};
}
