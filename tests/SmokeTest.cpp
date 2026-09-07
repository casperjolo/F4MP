// End-to-end test of the dedicated server over a real UDP socket.
//
// Launches F4MPServer.exe on a private port, drives its console over stdin and talks the wire
// protocol as one or more clients. Covers the handshake, peer visibility, state relay, chat and
// its flood control, renaming, malformed-state rejection, bots and kicking.
//
// Everything here is engine-free, so it runs headless. The client plugin's engine calls
// (spawning clones, animation) cannot be tested this way; they need the game.

#include "TestHarness.hpp"

#include "f4mp/Protocol.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace f4mp;
using namespace f4mp::test;

namespace
{
	constexpr std::uint16_t TEST_PORT = 27099;
	constexpr const char* TEST_SERVER_NAME = "F4MP Test Server";
	constexpr const char* TEST_MOTD = "Smoke test";

	using Clock = std::chrono::steady_clock;

	void SleepMs(int a_ms)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(a_ms));
	}

	// ---- the server under test ----------------------------------------------------

	class ServerProcess
	{
	public:
		bool Start(const std::filesystem::path& a_exe)
		{
			_config = std::filesystem::temp_directory_path() / "f4mp_smoke_server.ini";
			{
				std::ofstream file(_config);
				file << "[Server]\n"
					 << "Name=" << TEST_SERVER_NAME << "\n"
					 << "Motd=" << TEST_MOTD << "\n"
					 << "Port=" << TEST_PORT << "\n"
					 << "MaxPlayers=8\n"
					 << "TickRate=30\n"
					 << "[Logging]\n"
					 << "File=\n"
					 << "Level=warn\n";
			}

			// _popen goes through cmd.exe: it wants native separators, and a command whose parts
			// are quoted has to be wrapped in one more pair of quotes.
			auto exe = a_exe;
			auto config = _config;
			exe.make_preferred();
			config.make_preferred();
			const auto command = "\"\"" + exe.string() + "\" --config \"" + config.string() + "\"\"";

			_pipe = _popen(command.c_str(), "w");
			if (!_pipe) {
				std::printf("could not launch: %s\n", command.c_str());
			}
			return _pipe != nullptr;
		}

		void Command(const std::string& a_line)
		{
			if (!_pipe) {
				return;
			}
			std::fprintf(_pipe, "%s\n", a_line.c_str());
			std::fflush(_pipe);
		}

		void Stop()
		{
			if (!_pipe) {
				return;
			}
			Command("stop");
			_pclose(_pipe);
			_pipe = nullptr;
			std::error_code ec;
			std::filesystem::remove(_config, ec);
		}

		~ServerProcess() { Stop(); }

	private:
		std::FILE* _pipe{ nullptr };
		std::filesystem::path _config;
	};

	// ---- a client that speaks the protocol ------------------------------------------

	class TestClient
	{
	public:
		bool Connect(int a_timeoutMs = 5000)
		{
			_host = enet_host_create(nullptr, 1, static_cast<std::size_t>(Channel::kCount), 0, 0);
			if (!_host) {
				return false;
			}
			ENetAddress address{};
			enet_address_set_host(&address, "127.0.0.1");
			address.port = TEST_PORT;
			_peer = enet_host_connect(_host, &address, static_cast<std::size_t>(Channel::kCount), 0);
			if (!_peer) {
				return false;
			}

			const auto deadline = Clock::now() + std::chrono::milliseconds(a_timeoutMs);
			while (Clock::now() < deadline) {
				ENetEvent event{};
				while (enet_host_service(_host, &event, 10) > 0) {
					if (event.type == ENET_EVENT_TYPE_CONNECT) {
						_connected = true;
						return true;
					}
					if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						enet_packet_destroy(event.packet);
					}
				}
			}
			return false;
		}

		void Disconnect()
		{
			if (_peer) {
				enet_peer_disconnect_now(_peer, 0);
				_peer = nullptr;
			}
			if (_host) {
				enet_host_destroy(_host);
				_host = nullptr;
			}
			_connected = false;
		}

		~TestClient() { Disconnect(); }

		template <class T>
		void Send(Channel a_channel, const T& a_msg)
		{
			const auto bytes = Encode(a_msg);
			const enet_uint32 flags = a_channel == Channel::kReliable ? ENET_PACKET_FLAG_RELIABLE : 0u;
			auto* packet = enet_packet_create(bytes.data(), bytes.size(), flags);
			enet_peer_send(_peer, static_cast<enet_uint8>(a_channel), packet);
			enet_host_flush(_host);
		}

		void Pump(int a_ms = 0)
		{
			const auto deadline = Clock::now() + std::chrono::milliseconds(a_ms);
			do {
				ENetEvent event{};
				while (_host && enet_host_service(_host, &event, 1) > 0) {
					switch (event.type) {
					case ENET_EVENT_TYPE_RECEIVE:
						_inbox.emplace_back(event.packet->data, event.packet->data + event.packet->dataLength);
						enet_packet_destroy(event.packet);
						break;
					case ENET_EVENT_TYPE_DISCONNECT:
						_disconnected = true;
						_peer = nullptr;
						break;
					default:
						break;
					}
				}
			} while (Clock::now() < deadline);
		}

		// Pops the first queued message with this id, waiting up to a_timeoutMs for one to arrive.
		std::optional<std::vector<std::uint8_t>> Take(MsgId a_id, int a_timeoutMs = 2000)
		{
			const auto deadline = Clock::now() + std::chrono::milliseconds(a_timeoutMs);
			for (;;) {
				for (auto it = _inbox.begin(); it != _inbox.end(); ++it) {
					if (!it->empty() && static_cast<MsgId>(it->front()) == a_id) {
						auto bytes = *it;
						_inbox.erase(it);
						return bytes;
					}
				}
				if (Clock::now() >= deadline) {
					return std::nullopt;
				}
				Pump(10);
			}
		}

		[[nodiscard]] std::size_t Count(MsgId a_id) const
		{
			std::size_t n = 0;
			for (const auto& bytes : _inbox) {
				if (!bytes.empty() && static_cast<MsgId>(bytes.front()) == a_id) {
					++n;
				}
			}
			return n;
		}

		// Chat messages from a specific sender still sitting in the inbox.
		[[nodiscard]] std::vector<std::string> ChatFrom(PlayerId a_from) const
		{
			std::vector<std::string> out;
			for (const auto& bytes : _inbox) {
				if (bytes.empty() || static_cast<MsgId>(bytes.front()) != MsgId::kChatBroadcast) {
					continue;
				}
				Reader r(bytes.data(), bytes.size());
				MsgId id{};
				(void)ReadHeader(r, id);
				ChatBroadcastMsg msg;
				if (msg.Read(r) && msg.from == a_from) {
					out.push_back(msg.text);
				}
			}
			return out;
		}

		// Drain the socket first: anything already sent to us but not yet read would otherwise
		// arrive after the clear and look like a response to whatever we do next.
		void Clear(int a_settleMs = 200)
		{
			Pump(a_settleMs);
			_inbox.clear();
		}

		[[nodiscard]] bool HeardChat(PlayerId a_from, std::string_view a_text) const
		{
			for (const auto& line : ChatFrom(a_from)) {
				if (line == a_text) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool WasDisconnected() const noexcept { return _disconnected; }

		// Handshake helper: says hello and returns the Welcome, or nothing on rejection.
		std::optional<WelcomeMsg> Join(const std::string& a_name, std::uint32_t a_protocol = PROTOCOL_VERSION)
		{
			HelloMsg hello;
			hello.protocolVersion = a_protocol;
			hello.gameVersion = 0x010B00F0;
			hello.name = a_name;
			Send(Channel::kReliable, hello);

			const auto bytes = Take(MsgId::kWelcome);
			if (!bytes) {
				return std::nullopt;
			}
			Reader r(bytes->data(), bytes->size());
			MsgId id{};
			(void)ReadHeader(r, id);
			WelcomeMsg welcome;
			if (!welcome.Read(r)) {
				return std::nullopt;
			}
			_id = welcome.id;
			return welcome;
		}

		[[nodiscard]] PlayerId Id() const noexcept { return _id; }

	private:
		ENetHost* _host{ nullptr };
		ENetPeer* _peer{ nullptr };
		bool _connected{ false };
		bool _disconnected{ false };
		PlayerId _id{ INVALID_PLAYER_ID };
		std::deque<std::vector<std::uint8_t>> _inbox;
	};

	// Decodes a message body, assuming the id byte is at the front.
	template <class T>
	std::optional<T> Decode(const std::vector<std::uint8_t>& a_bytes)
	{
		Reader r(a_bytes.data(), a_bytes.size());
		MsgId id{};
		if (!ReadHeader(r, id)) {
			return std::nullopt;
		}
		T msg;
		if (!msg.Read(r)) {
			return std::nullopt;
		}
		return msg;
	}

	PlayerState MakeState(float a_x, float a_y, float a_z)
	{
		PlayerState s;
		s.position = Vec3{ a_x, a_y, a_z };
		s.yaw = 0.5f;
		s.worldspaceId = 0x0000003C;
		s.cellId = 0x0000DD41;
		s.health = 100.0f;
		s.maxHealth = 100.0f;
		return s;
	}

	// ---- the tests -------------------------------------------------------------------

	void TestHandshake(ServerProcess&)
	{
		Suite("Handshake");

		TestClient client;
		CHECK(client.Connect(), "a client can connect");
		const auto welcome = client.Join("Akira");
		CHECK(welcome.has_value(), "Hello is answered with Welcome");
		if (welcome) {
			CHECK(welcome->id != INVALID_PLAYER_ID && welcome->id != SERVER_ID, "the server assigns a real player id");
			CHECK(welcome->serverName == TEST_SERVER_NAME, "Welcome carries the configured server name");
			CHECK(welcome->motd == TEST_MOTD, "Welcome carries the configured MOTD");
			CHECK(welcome->tickRate == 30, "Welcome carries the configured tick rate");
		}
		client.Disconnect();
	}

	void TestProtocolMismatch(ServerProcess&)
	{
		Suite("Protocol mismatch");

		TestClient client;
		CHECK(client.Connect(), "the client connects");
		const auto welcome = client.Join("Stranger", PROTOCOL_VERSION + 99);
		CHECK(!welcome.has_value(), "a mismatched client is not welcomed");

		const auto bytes = client.Take(MsgId::kReject);
		CHECK(bytes.has_value(), "a mismatched client is rejected");
		if (bytes) {
			const auto reject = Decode<RejectMsg>(*bytes);
			CHECK(reject && reject->reason.find("Protocol") != std::string::npos,
				"the rejection says it is a protocol mismatch");
		}
		client.Disconnect();
	}

	void TestPeerVisibilityAndState(ServerProcess&)
	{
		Suite("Peers and state relay");

		TestClient a;
		TestClient b;
		CHECK(a.Connect() && a.Join("PlayerA").has_value(), "player A joins");
		a.Pump(100);

		CHECK(b.Connect() && b.Join("PlayerB").has_value(), "player B joins");
		b.Pump(100);
		a.Pump(200);

		// A learns about B joining after it.
		const auto joined = a.Take(MsgId::kPlayerJoined);
		CHECK(joined.has_value(), "A is told that B joined");
		if (joined) {
			const auto msg = Decode<PlayerJoinedMsg>(*joined);
			CHECK(msg && msg->id == b.Id(), "the join carries B's id");
			CHECK(msg && msg->name == "PlayerB", "the join carries B's name");
		}

		// B was told about A during its own handshake.
		bool sawA = false;
		while (const auto bytes = b.Take(MsgId::kPlayerJoined, 200)) {
			const auto msg = Decode<PlayerJoinedMsg>(*bytes);
			if (msg && msg->id == a.Id()) {
				sawA = true;
			}
		}
		CHECK(sawA, "B is told about the player who was already there");

		// A's state reaches B, stamped with the server clock.
		b.Clear();
		a.Send(Channel::kUnreliable, PlayerStateMsg{ MakeState(1000.0f, 2000.0f, 300.0f) });
		const auto update = b.Take(MsgId::kPlayerStateUpdate);
		CHECK(update.has_value(), "A's state reaches B");
		std::uint32_t firstStamp = 0;
		if (update) {
			const auto msg = Decode<PlayerStateUpdateMsg>(*update);
			CHECK(msg && msg->id == a.Id(), "the update is attributed to A");
			CHECK(msg && Near(msg->state.position.x, 1000.0) && Near(msg->state.position.y, 2000.0),
				"the position survives the round trip");
			CHECK(msg && msg->state.cellId == 0x0000DD41u, "the cell id survives the round trip");
			if (msg) {
				firstStamp = msg->timeMs;
			}
		}

		SleepMs(120);
		a.Send(Channel::kUnreliable, PlayerStateMsg{ MakeState(1100.0f, 2000.0f, 300.0f) });
		const auto second = b.Take(MsgId::kPlayerStateUpdate);
		CHECK(second.has_value(), "a second state also arrives");
		if (second) {
			const auto msg = Decode<PlayerStateUpdateMsg>(*second);
			CHECK(msg && msg->timeMs > firstStamp, "server timestamps advance between updates");
			CHECK(msg && (msg->timeMs - firstStamp) >= 100 && (msg->timeMs - firstStamp) < 1000,
				"the timestamp gap matches the real delay");
		}

		// A must not be told about its own state.
		a.Clear();
		a.Send(Channel::kUnreliable, PlayerStateMsg{ MakeState(1200.0f, 2000.0f, 300.0f) });
		a.Pump(200);
		CHECK(a.Count(MsgId::kPlayerStateUpdate) == 0, "a player is not echoed its own state");

		// Leaving is announced.
		b.Disconnect();
		const auto left = a.Take(MsgId::kPlayerLeft);
		CHECK(left.has_value(), "A is told that B left");
		if (left) {
			const auto msg = Decode<PlayerLeftMsg>(*left);
			CHECK(msg && msg->id == b.Id(), "the departure carries B's id");
		}
		a.Disconnect();
	}

	void TestMalformedState(ServerProcess&)
	{
		Suite("Malformed state rejection");

		TestClient a;
		TestClient b;
		a.Connect();
		a.Join("NaNSender");
		b.Connect();
		b.Join("Watcher");
		a.Pump(100);
		b.Pump(100);
		b.Clear();

		auto bad = MakeState(0.0f, 0.0f, 0.0f);
		bad.position.x = std::numeric_limits<float>::quiet_NaN();
		a.Send(Channel::kUnreliable, PlayerStateMsg{ bad });
		b.Pump(300);
		CHECK(b.Count(MsgId::kPlayerStateUpdate) == 0, "a NaN position is not relayed");

		auto infinite = MakeState(0.0f, 0.0f, 0.0f);
		infinite.health = std::numeric_limits<float>::infinity();
		a.Send(Channel::kUnreliable, PlayerStateMsg{ infinite });
		b.Pump(300);
		CHECK(b.Count(MsgId::kPlayerStateUpdate) == 0, "an infinite health value is not relayed");

		a.Send(Channel::kUnreliable, PlayerStateMsg{ MakeState(5.0f, 6.0f, 7.0f) });
		CHECK(b.Take(MsgId::kPlayerStateUpdate).has_value(), "a good state still gets through afterwards");

		a.Disconnect();
		b.Disconnect();
	}

	void TestChat(ServerProcess& a_server)
	{
		Suite("Chat");

		TestClient a;
		TestClient b;
		a.Connect();
		a.Join("Talker");
		b.Connect();
		b.Join("Listener");
		a.Pump(100);
		b.Pump(100);
		b.Clear();

		a.Send(Channel::kReliable, ChatMsg{ "hello there" });
		const auto heard = b.Take(MsgId::kChatBroadcast);
		CHECK(heard.has_value(), "chat reaches the other player");
		if (heard) {
			const auto msg = Decode<ChatBroadcastMsg>(*heard);
			CHECK(msg && msg->from == a.Id(), "chat is attributed to the sender");
			CHECK(msg && msg->text == "hello there", "the text survives unquoted and unchanged");
		}

		// The sender hears itself, so the client can echo it locally.
		a.Clear();
		a.Send(Channel::kReliable, ChatMsg{ "own words" });
		a.Pump(300);
		CHECK(a.HeardChat(a.Id(), "own words"), "the sender also receives its own chat");

		// Control characters are stripped rather than passed through to other clients.
		b.Clear();
		a.Send(Channel::kReliable, ChatMsg{ "clean\nline\ttext" });
		b.Pump(300);
		CHECK(b.HeardChat(a.Id(), "cleanlinetext"), "control characters are stripped from chat");

		// The server console can talk, and its messages come from id 0.
		b.Clear();
		a_server.Command("say server speaking");
		b.Pump(500);
		CHECK(b.HeardChat(SERVER_ID, "server speaking"), "the server console can broadcast chat as id 0");

		a.Disconnect();
		b.Disconnect();
	}

	void TestChatFlood(ServerProcess&)
	{
		Suite("Chat flood control");

		// A fresh client, so the rate-limit window is untouched by earlier tests.
		TestClient spammer;
		TestClient watcher;
		spammer.Connect();
		spammer.Join("Spammer");
		watcher.Connect();
		watcher.Join("Bystander");
		spammer.Pump(100);
		watcher.Pump(100);
		watcher.Clear();
		spammer.Clear();

		constexpr int sent = 10;
		for (int i = 0; i < sent; ++i) {
			spammer.Send(Channel::kReliable, ChatMsg{ "spam " + std::to_string(i) });
		}
		watcher.Pump(700);
		spammer.Pump(200);

		const auto relayed = watcher.ChatFrom(spammer.Id());
		CHECK_MSG(relayed.size() == 6, "at most 6 messages per 5 s are relayed",
			"relayed " + std::to_string(relayed.size()) + " of " + std::to_string(sent));

		bool warned = false;
		for (const auto& line : spammer.ChatFrom(SERVER_ID)) {
			if (line == "Slow down.") {
				warned = true;
			}
		}
		CHECK(warned, "the flooder is told to slow down");

		bool warnedTwice = false;
		int warnings = 0;
		for (const auto& line : spammer.ChatFrom(SERVER_ID)) {
			if (line == "Slow down.") {
				++warnings;
			}
		}
		warnedTwice = warnings > 1;
		CHECK(!warnedTwice, "the warning is sent once, not once per dropped message");

		spammer.Disconnect();
		watcher.Disconnect();
	}

	void TestRename(ServerProcess&)
	{
		Suite("Renaming");

		TestClient a;
		TestClient b;
		a.Connect();
		a.Join("Wastelander");
		b.Connect();
		b.Join("Observer");
		a.Pump(100);
		b.Pump(100);
		b.Clear();

		a.Send(Channel::kReliable, SetNameMsg{ "Akira" });
		const auto renamed = b.Take(MsgId::kPlayerRenamed);
		CHECK(renamed.has_value(), "a rename is broadcast to other players");
		if (renamed) {
			const auto msg = Decode<PlayerRenamedMsg>(*renamed);
			CHECK(msg && msg->id == a.Id(), "the rename carries the right player id");
			CHECK(msg && msg->name == "Akira", "the rename carries the new name");
		}

		// Renaming to the same thing must not spam the wire.
		b.Clear();
		a.Send(Channel::kReliable, SetNameMsg{ "Akira" });
		b.Pump(300);
		CHECK(b.Count(MsgId::kPlayerRenamed) == 0, "renaming to the same name is not rebroadcast");

		// Names are sanitised server-side.
		b.Clear();
		a.Send(Channel::kReliable, SetNameMsg{ "  Nick\nValentine  " });
		const auto sanitized = b.Take(MsgId::kPlayerRenamed);
		CHECK(sanitized.has_value(), "a messy name is still accepted");
		if (sanitized) {
			const auto msg = Decode<PlayerRenamedMsg>(*sanitized);
			CHECK(msg && msg->name == "NickValentine", "the name is sanitised before broadcast");
		}

		a.Disconnect();
		b.Disconnect();
	}

	void TestBots(ServerProcess& a_server)
	{
		Suite("Bots");

		TestClient client;
		client.Connect();
		client.Join("BotWatcher");
		client.Pump(100);

		// The bot mirrors the first player that has sent a state.
		const auto myState = MakeState(-1000.0f, 500.0f, 90.0f);
		client.Send(Channel::kUnreliable, PlayerStateMsg{ myState });
		client.Pump(100);
		client.Clear();

		a_server.Command("bot add mirror Shadow");
		const auto joined = client.Take(MsgId::kPlayerJoined, 3000);
		CHECK(joined.has_value(), "a bot announces itself like a player");
		PlayerId botId = INVALID_PLAYER_ID;
		if (joined) {
			const auto msg = Decode<PlayerJoinedMsg>(*joined);
			CHECK(msg && msg->name == "Shadow", "the bot uses the name it was given");
			if (msg) {
				botId = msg->id;
			}
		}

		// Keep feeding our own state, as a real client would, and collect the bot's.
		int updates = 0;
		bool offsetOk = false;
		const auto deadline = Clock::now() + std::chrono::milliseconds(1000);
		while (Clock::now() < deadline) {
			client.Send(Channel::kUnreliable, PlayerStateMsg{ myState });
			client.Pump(50);
			while (const auto bytes = client.Take(MsgId::kPlayerStateUpdate, 0)) {
				const auto msg = Decode<PlayerStateUpdateMsg>(*bytes);
				if (msg && msg->id == botId) {
					++updates;
					if (Near(msg->state.position.x, myState.position.x + 200.0, 1.0) &&
						Near(msg->state.position.y, myState.position.y, 1.0)) {
						offsetOk = true;
					}
				}
			}
		}
		CHECK_MSG(updates >= 10, "the bot streams state at roughly 20 Hz",
			"received " + std::to_string(updates) + " updates in 1 s");
		CHECK(offsetOk, "a mirror bot stands 200 units east of the player it follows");

		// An orbit bot moves under its own steam.
		a_server.Command("bot add orbit Walker");
		client.Take(MsgId::kPlayerJoined, 2000);
		client.Clear();

		float minX = 1e9f;
		float maxX = -1e9f;
		const auto orbitDeadline = Clock::now() + std::chrono::milliseconds(2500);
		while (Clock::now() < orbitDeadline) {
			client.Send(Channel::kUnreliable, PlayerStateMsg{ myState });
			client.Pump(50);
			while (const auto bytes = client.Take(MsgId::kPlayerStateUpdate, 0)) {
				const auto msg = Decode<PlayerStateUpdateMsg>(*bytes);
				if (msg && msg->id != botId && msg->id != client.Id()) {
					minX = std::min(minX, msg->state.position.x);
					maxX = std::max(maxX, msg->state.position.x);
				}
			}
		}
		CHECK_MSG(maxX - minX > 50.0f, "an orbit bot actually moves",
			"x travelled " + std::to_string(maxX - minX) + " units");

		// Bots go away on command.
		client.Clear();
		a_server.Command("bot remove all");
		const auto gone = client.Take(MsgId::kPlayerLeft, 2000);
		CHECK(gone.has_value(), "removed bots announce their departure");

		client.Disconnect();
	}

	void TestKick(ServerProcess& a_server)
	{
		Suite("Kicking");

		TestClient client;
		client.Connect();
		const auto welcome = client.Join("Doomed");
		CHECK(welcome.has_value(), "the victim joins");
		client.Pump(100);
		client.Clear();

		a_server.Command("kick " + std::to_string(client.Id()) + " go away");
		const auto reject = client.Take(MsgId::kReject, 3000);
		CHECK(reject.has_value(), "a kicked player is told why");
		if (reject) {
			const auto msg = Decode<RejectMsg>(*reject);
			CHECK(msg && msg->reason.find("go away") != std::string::npos, "the kick reason reaches the player");
		}
		client.Pump(1000);
		CHECK(client.WasDisconnected(), "the kicked player is actually disconnected");
	}
}

int main(int argc, char** argv)
{
	std::filesystem::path exe = argc > 1 ? argv[1] : F4MP_SERVER_EXE;
	if (!std::filesystem::exists(exe)) {
		std::printf("server executable not found: %s\n", exe.string().c_str());
		return 2;
	}

	if (enet_initialize() != 0) {
		std::printf("enet_initialize failed\n");
		return 2;
	}

	std::printf("F4MP smoke test (protocol v%u)\nserver: %s\n", PROTOCOL_VERSION, exe.string().c_str());

	ServerProcess server;
	if (!server.Start(exe)) {
		std::printf("could not start the server process\n");
		enet_deinitialize();
		return 2;
	}

	// Wait for the socket to be listening.
	bool up = false;
	for (int attempt = 0; attempt < 20 && !up; ++attempt) {
		TestClient probe;
		up = probe.Connect(500);
		probe.Disconnect();
		if (!up) {
			SleepMs(250);
		}
	}
	if (!up) {
		std::printf("the server never accepted a connection on port %u\n", TEST_PORT);
		server.Stop();
		enet_deinitialize();
		return 2;
	}

	TestHandshake(server);
	TestProtocolMismatch(server);
	TestPeerVisibilityAndState(server);
	TestMalformedState(server);
	TestChat(server);
	TestChatFlood(server);
	TestRename(server);
	TestBots(server);
	TestKick(server);

	server.Stop();
	enet_deinitialize();
	return Summary();
}
