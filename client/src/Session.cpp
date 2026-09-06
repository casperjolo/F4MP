#include "PCH.hpp"

#include "Session.hpp"

#include "Game/GameUtil.hpp"

#include <charconv>

namespace f4mp::client
{
	namespace
	{
		constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(10);
		constexpr auto HEARTBEAT_INTERVAL = std::chrono::seconds(1);
		constexpr float MOVE_EPSILON = 0.5f;   // game units
		constexpr float YAW_EPSILON = 0.005f;  // radians
		constexpr float HEALTH_EPSILON = 0.5f;

		bool Changed(const PlayerState& a_a, const PlayerState& a_b) noexcept
		{
			const auto dx = a_a.position.x - a_b.position.x;
			const auto dy = a_a.position.y - a_b.position.y;
			const auto dz = a_a.position.z - a_b.position.z;
			if (dx * dx + dy * dy + dz * dz > MOVE_EPSILON * MOVE_EPSILON) {
				return true;
			}
			return std::fabs(a_a.yaw - a_b.yaw) > YAW_EPSILON ||
			       a_a.worldspaceId != a_b.worldspaceId ||
			       a_a.cellId != a_b.cellId ||
			       a_a.flags != a_b.flags ||
			       std::fabs(a_a.health - a_b.health) > HEALTH_EPSILON;
		}
	}

	Session& Session::Get()
	{
		static Session instance;
		return instance;
	}

	void Session::Init(Config a_config)
	{
		_config = std::move(a_config);
		_remotes.SetInterpDelay(_config.interpDelayMs);

		PrologueSkip::Settings skip;
		skip.enabled = _config.skipPrologue;
		skip.startDelay = _config.skipDelay;
		skip.podTimeout = _config.podTimeout;
		skip.commands = _config.skipCommands;
		skip.chargen = _config.chargen;
		skip.chargenMode = _config.chargenMode;
		_skip.Configure(std::move(skip));

		_net.onConnected = [this]() {
			HelloMsg hello;
			hello.gameVersion = F4SE::GetRuntimeVersion().Pack<std::uint32_t>();
			hello.name = _config.playerName.empty() ? game::GetPlayerName() : _config.playerName;
			_sentName = hello.name;
			_net.Send(Channel::kReliable, Encode(hello));
		};

		_net.onDisconnected = [this](const std::string& a_reason) {
			_welcomed = false;
			_myId = INVALID_PLAYER_ID;
			_serverName.clear();
			_hasLastSent = false;
			_remotes.Clear();
			Print("[F4MP] " + a_reason);
		};

		_net.onPacket = [this](std::span<const std::uint8_t> a_bytes) {
			HandlePacket(a_bytes);
		};
	}

	// ---- F4SE lifecycle ---------------------------------------------------------

	void Session::OnGameDataReady()
	{
		REX::LogInformation("Game data ready"sv);
	}

	void Session::OnEnterWorld()
	{
		REX::LogInformation("Entered world"sv);
		_inWorld = true;
		_hasLastSent = false;
		_manualDisconnect = false;
		_lastReconnectAttempt = {};

		// Actors and dynamic base forms from a previous game no longer exist; they come back
		// on the next Tick.
		_remotes.ForgetActors();
	}

	void Session::OnNewGame()
	{
		OnEnterWorld();
		_skip.OnNewGame();
	}

	void Session::OnLeaveWorld()
	{
		REX::LogInformation("Leaving world"sv);
		_inWorld = false;
		_skip.OnLeaveWorld();
		_remotes.ForgetActors();
	}

	void Session::OnPreSave()
	{
		// Keep clones out of the save file; they come back on the next frame.
		_remotes.DespawnAll();
	}

	// ---- per frame ------------------------------------------------------------------

	void Session::Tick()
	{
		_net.Pump();

		if (!_inWorld) {
			return;
		}

		_skip.Update();

		if (_net.GetState() == NetClient::State::kDisconnected && _config.autoConnect && !_manualDisconnect) {
			const auto now = std::chrono::steady_clock::now();
			if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL) {
				_lastReconnectAttempt = now;
				Connect();
			}
		}

		if (IsConnected()) {
			SendState(false);
			SyncName();
			_remotes.Update();
		}
	}

	// A new character has no name until the SPECIAL form; tell the server when it changes.
	void Session::SyncName()
	{
		constexpr auto NAME_CHECK_INTERVAL = std::chrono::seconds(2);
		const auto now = std::chrono::steady_clock::now();
		if (now - _lastNameCheck < NAME_CHECK_INTERVAL) {
			return;
		}
		_lastNameCheck = now;

		const auto name = _config.playerName.empty() ? game::GetPlayerName() : _config.playerName;
		if (name == _sentName) {
			return;
		}
		_sentName = name;
		_net.Send(Channel::kReliable, Encode(SetNameMsg{ name }));
	}

	void Session::SendState(bool a_force)
	{
		PlayerState state;
		if (!_local.Sample(state)) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		const auto interval = std::chrono::milliseconds(1000 / std::max<std::uint32_t>(1, _config.sendRate));
		if (!a_force && now - _lastSend < interval) {
			return;
		}

		const auto heartbeat = now - _lastSend >= HEARTBEAT_INTERVAL;
		if (!a_force && !heartbeat && _hasLastSent && !Changed(state, _lastSent)) {
			return;
		}

		_net.Send(Channel::kUnreliable, Encode(PlayerStateMsg{ state }));
		_lastSent = state;
		_hasLastSent = true;
		_lastSend = now;
	}

	// ---- connection -----------------------------------------------------------------

	void Session::Connect(bool a_manual)
	{
		if (!_inWorld) {
			if (a_manual) {
				game::ConsolePrint("[F4MP] Load a save or start a new game first.");
			}
			return;
		}

		if (a_manual) {
			_manualDisconnect = false;
			_lastReconnectAttempt = std::chrono::steady_clock::now();
			if (_net.GetState() != NetClient::State::kDisconnected) {
				_net.Disconnect();
			}
		} else if (_net.GetState() != NetClient::State::kDisconnected) {
			return;
		}

		Print("[F4MP] Connecting to " + ServerAddress() + "...");
		if (!_net.Connect(_config.host, _config.port)) {
			Print("[F4MP] Could not start connection (see F4MP.log)");
		}
	}

	void Session::Disconnect(bool a_manual)
	{
		if (a_manual) {
			_manualDisconnect = true;
			if (_net.GetState() == NetClient::State::kDisconnected) {
				game::ConsolePrint("[F4MP] Not connected.");
				return;
			}
		}
		_net.Disconnect();
	}

	void Session::SendChat(const std::string& a_text)
	{
		if (a_text.empty()) {
			return;
		}
		if (!IsConnected()) {
			game::ConsolePrint("[F4MP] Not connected.");
			return;
		}
		ChatMsg msg{ a_text };
		if (msg.text.size() > MAX_CHAT_BYTES) {
			msg.text.resize(MAX_CHAT_BYTES);
		}
		_net.Send(Channel::kReliable, Encode(msg));
	}

	// ---- console helpers ------------------------------------------------------------

	bool Session::SetServer(std::string_view a_hostPort)
	{
		std::string host(a_hostPort);
		auto port = _config.port;

		// "host:port"; a bare IPv6 literal (more than one colon) is taken as host only.
		const auto colon = host.rfind(':');
		if (colon != std::string::npos && host.find(':') == colon) {
			const auto portText = host.substr(colon + 1);
			unsigned int value{};
			const auto res = std::from_chars(portText.data(), portText.data() + portText.size(), value);
			if (res.ec != std::errc{} || res.ptr != portText.data() + portText.size() || value == 0 || value > 65535) {
				return false;
			}
			port = static_cast<std::uint16_t>(value);
			host.resize(colon);
		}
		if (host.empty()) {
			return false;
		}

		_config.host = std::move(host);
		_config.port = port;
		REX::LogInformation("Server set to {}"sv, ServerAddress());
		return true;
	}

	void Session::SetPlayerName(std::string_view a_name)
	{
		_config.playerName = SanitizeName(std::string(a_name));
		game::ConsolePrint("[F4MP] Name set to \"" + _config.playerName + "\".");
		_lastNameCheck = {}; // picked up by SyncName on the next frame
	}

	void Session::PrintStatus() const
	{
		std::string line = "[F4MP] ";
		switch (_net.GetState()) {
		case NetClient::State::kConnected:
			if (_welcomed) {
				line += "Connected to \"" + _serverName + "\" (" + ServerAddress() + ") as #" + std::to_string(_myId) +
				        ", ping " + std::to_string(_net.GetPingMs()) + " ms, " +
				        std::to_string(_remotes.Count()) + " other player(s)";
			} else {
				line += "Connected to " + ServerAddress() + ", waiting for the server to answer";
			}
			break;
		case NetClient::State::kConnecting:
			line += "Connecting to " + ServerAddress() + "...";
			break;
		case NetClient::State::kDisconnected:
		default:
			line += "Disconnected (" + ServerAddress() + ")";
			if (!_inWorld) {
				line += ", not in a game yet";
			} else if (_manualDisconnect) {
				line += ", auto-connect paused until \"f4mp connect\"";
			} else if (_config.autoConnect) {
				line += ", auto-connect on";
			}
			break;
		}
		game::ConsolePrint(line);
	}

	void Session::PrintPlayers() const
	{
		if (!IsConnected()) {
			game::ConsolePrint("[F4MP] Not connected.");
			return;
		}

		game::ConsolePrint("[F4MP] Players on \"" + _serverName + "\":");
		game::ConsolePrint("  #" + std::to_string(_myId) + " " +
		                   (_config.playerName.empty() ? game::GetPlayerName() : _config.playerName) + " (you)");
		for (const auto& [id, player] : _remotes.All()) {
			std::string where = "no position yet";
			if (player.hasState) {
				where = player.IsSpawned() ? "in view" : "elsewhere";
			}
			game::ConsolePrint("  #" + std::to_string(id) + " " + player.name + " (" + where + ")");
		}
	}

	// ---- incoming -------------------------------------------------------------------

	void Session::HandlePacket(std::span<const std::uint8_t> a_bytes)
	{
		Reader reader(a_bytes);
		MsgId id{};
		if (!ReadHeader(reader, id)) {
			return;
		}

		switch (id) {
		case MsgId::kWelcome: {
			WelcomeMsg msg;
			if (!msg.Read(reader)) {
				return;
			}
			_myId = msg.id;
			_serverName = msg.serverName;
			_welcomed = true;
			REX::LogInformation("Welcomed by \"{}\" as #{} (tick {} Hz)"sv, msg.serverName, msg.id, msg.tickRate);
			Print("[F4MP] Connected to " + msg.serverName + " as player #" + std::to_string(msg.id));
			if (!msg.motd.empty()) {
				Print("[F4MP] " + msg.motd);
			}
			SendState(true);
			break;
		}

		case MsgId::kReject: {
			RejectMsg msg;
			if (msg.Read(reader)) {
				REX::LogWarning("Rejected by server: {}"sv, msg.reason);
				Print("[F4MP] Rejected: " + msg.reason);
			}
			break;
		}

		case MsgId::kPlayerJoined: {
			PlayerJoinedMsg msg;
			if (msg.Read(reader) && msg.id != _myId) {
				_remotes.Add(msg.id, msg.name);
			}
			break;
		}

		case MsgId::kPlayerLeft: {
			PlayerLeftMsg msg;
			if (msg.Read(reader)) {
				_remotes.Remove(msg.id);
			}
			break;
		}

		case MsgId::kPlayerRenamed: {
			PlayerRenamedMsg msg;
			if (!msg.Read(reader)) {
				return;
			}
			if (msg.id == _myId) {
				Print("[F4MP] You are now known as " + msg.name);
			} else {
				const auto old = _remotes.NameOf(msg.id);
				_remotes.Rename(msg.id, msg.name);
				Print("[F4MP] " + old + " is now known as " + msg.name);
			}
			break;
		}

		case MsgId::kPlayerStateUpdate: {
			PlayerStateUpdateMsg msg;
			if (msg.Read(reader) && msg.id != _myId) {
				_remotes.ApplyState(msg.id, msg.state);
			}
			break;
		}

		case MsgId::kChatBroadcast: {
			ChatBroadcastMsg msg;
			if (!msg.Read(reader)) {
				return;
			}
			if (msg.from == SERVER_ID) {
				Print("[Server] " + msg.text);
			} else {
				const auto name = msg.from == _myId ? std::string("You") : _remotes.NameOf(msg.from);
				Print(name + ": " + msg.text);
			}
			break;
		}

		default:
			REX::LogWarning("Unknown message id {}"sv, static_cast<int>(id));
			break;
		}
	}

	void Session::Print(const std::string& a_text) const
	{
		REX::LogInformation("{}"sv, a_text);
		if (_config.consoleMessages) {
			game::ConsolePrint(a_text);
		}
	}

	std::string Session::ServerAddress() const
	{
		return _config.host + ":" + std::to_string(_config.port);
	}
}
