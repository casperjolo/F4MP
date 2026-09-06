#pragma once

#include "Config.hpp"
#include "Net/NetClient.hpp"
#include "Sync/LocalPlayer.hpp"
#include "Sync/RemotePlayers.hpp"

namespace f4mp::client
{
	// Ties the network connection to the game: decides when to connect, what to send each
	// frame, and dispatches server messages to the remote player set.
	class Session
	{
	public:
		[[nodiscard]] static Session& Get();

		void Init(Config a_config);

		// F4SE lifecycle
		void OnGameDataReady();
		void OnEnterWorld(); // save loaded / new game started
		void OnLeaveWorld(); // about to load a save
		void OnPreSave();

		// Once per frame on the game thread.
		void Tick();

		// a_manual: requested from the console. Reconnects if already connected and clears a
		// previous manual disconnect so auto-connect resumes.
		void Connect(bool a_manual = false);
		// a_manual: stay disconnected until "f4mp connect" or the next save load.
		void Disconnect(bool a_manual = false);
		void SendChat(const std::string& a_text);

		// Console helpers. SetServer accepts "host" or "host:port"; false if the port is bad.
		bool SetServer(std::string_view a_hostPort);
		void SetPlayerName(std::string_view a_name);
		void PrintStatus() const;
		void PrintPlayers() const;

		[[nodiscard]] bool IsConnected() const noexcept { return _net.IsConnected() && _welcomed; }

	private:
		Session() = default;

		void HandlePacket(std::span<const std::uint8_t> a_bytes);
		void SendState(bool a_force);
		void Print(const std::string& a_text) const;
		[[nodiscard]] std::string ServerAddress() const;

		Config _config;
		NetClient _net;
		LocalPlayer _local;
		RemotePlayers _remotes;

		PlayerId _myId{ INVALID_PLAYER_ID };
		std::string _serverName;
		bool _inWorld{ false };
		bool _welcomed{ false };
		bool _manualDisconnect{ false };

		std::chrono::steady_clock::time_point _lastSend{};
		std::chrono::steady_clock::time_point _lastReconnectAttempt{};
		PlayerState _lastSent{};
		bool _hasLastSent{ false };
	};
}
