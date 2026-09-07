// Headless tests for everything that does not need the game: packet encoding, message
// round-trips, the reader's bounds checks, name sanitising, INI parsing and snapshot
// interpolation. Run by tests/run_tests, no server required.

#include "TestHarness.hpp"

#include "f4mp/Ini.hpp"
#include "f4mp/Interp.hpp"
#include "f4mp/Packet.hpp"
#include "f4mp/Protocol.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace f4mp;
using namespace f4mp::test;

namespace
{
	void TestPacket()
	{
		Suite("Packet writer / reader");

		Writer w;
		w.U8(0x7F);
		w.U16(0xBEEF);
		w.U32(0xDEADBEEF);
		w.I32(-123456);
		w.F32(1.5f);
		w.Bool(true);
		w.Str("hello");
		const auto bytes = w.Data();

		Reader r(bytes.data(), bytes.size());
		CHECK(r.U8() == 0x7F, "u8 round-trips");
		CHECK(r.U16() == 0xBEEF, "u16 round-trips");
		CHECK(r.U32() == 0xDEADBEEF, "u32 round-trips");
		CHECK(r.I32() == -123456, "i32 round-trips (negative)");
		CHECK(r.F32() == 1.5f, "f32 round-trips");
		CHECK(r.Bool(), "bool round-trips");
		CHECK(r.Str() == "hello", "string round-trips");
		CHECK(r.Ok(), "reader still healthy at the end");
		CHECK(r.Remaining() == 0, "nothing left over");

		// Little-endian on the wire, whatever the compiler does with the struct.
		Writer w2;
		w2.U32(0x01020304);
		CHECK(w2.Data()[0] == 0x04 && w2.Data()[3] == 0x01, "u32 is little-endian on the wire");

		// Reading past the end must fail, not read rubbish or crash.
		Reader empty(bytes.data(), 2);
		empty.U32();
		CHECK(!empty.Ok(), "reading past the end sets the failure flag");

		Reader truncated(bytes.data(), bytes.size() - 3);
		for (int i = 0; i < 8; ++i) {
			(void)truncated.U32();
		}
		CHECK(!truncated.Ok(), "a truncated buffer never reads past its end");

		// A string claiming more bytes than the packet holds must be refused.
		Writer liar;
		liar.U16(4096);
		liar.U8('a');
		Reader lied(liar.Data().data(), liar.Data().size());
		const auto huge = lied.Str();
		CHECK(huge.empty() && !lied.Ok(), "an over-long string length is rejected");

		// Strings are capped rather than truncating the stream.
		Writer big;
		big.Str(std::string(MAX_STRING_BYTES + 500, 'x'));
		Reader bigR(big.Data().data(), big.Data().size());
		CHECK(bigR.Str().size() == MAX_STRING_BYTES, "over-long strings are clamped when written");
	}

	void TestMessages()
	{
		Suite("Message round-trips");

		{
			HelloMsg out;
			out.gameVersion = 0x010B00F0;
			out.name = "Akira";
			const auto bytes = Encode(out);
			Reader r(bytes.data(), bytes.size());
			MsgId id{};
			CHECK(ReadHeader(r, id) && id == MsgId::kHello, "Hello carries its id");
			HelloMsg in;
			CHECK(in.Read(r), "Hello decodes");
			CHECK(in.protocolVersion == PROTOCOL_VERSION, "Hello: protocol version");
			CHECK(in.gameVersion == out.gameVersion, "Hello: game version");
			CHECK(in.name == out.name, "Hello: name");
		}

		{
			PlayerState s;
			s.position = Vec3{ -88696.0f, 91532.5f, 8903.25f };
			s.yaw = 1.75f;
			s.worldspaceId = 0x0000003C;
			s.cellId = 0x0000DD41;
			s.flags = kStateSneaking | kStateWeaponDrawn;
			s.health = 95.0f;
			s.maxHealth = 110.0f;

			PlayerStateUpdateMsg out{ 7, 123456789u, s };
			const auto bytes = Encode(out);
			Reader r(bytes.data(), bytes.size());
			MsgId id{};
			(void)ReadHeader(r, id);
			CHECK(id == MsgId::kPlayerStateUpdate, "PlayerStateUpdate carries its id");
			PlayerStateUpdateMsg in;
			CHECK(in.Read(r), "PlayerStateUpdate decodes");
			CHECK(in.id == 7, "state update: player id");
			CHECK(in.timeMs == 123456789u, "state update: server timestamp");
			CHECK(in.state.position.x == s.position.x && in.state.position.z == s.position.z, "state update: position");
			CHECK(in.state.yaw == s.yaw, "state update: yaw");
			CHECK(in.state.cellId == s.cellId && in.state.worldspaceId == s.worldspaceId, "state update: cell / worldspace");
			CHECK(in.state.flags == s.flags, "state update: flags");
			CHECK(in.state.health == s.health && in.state.maxHealth == s.maxHealth, "state update: health");
			CHECK(r.Remaining() == 0, "state update consumes exactly its payload");
		}

		{
			const auto bytes = Encode(ChatBroadcastMsg{ SERVER_ID, "Welcome to the Commonwealth." });
			Reader r(bytes.data(), bytes.size());
			MsgId id{};
			(void)ReadHeader(r, id);
			ChatBroadcastMsg in;
			CHECK(id == MsgId::kChatBroadcast && in.Read(r), "ChatBroadcast decodes");
			CHECK(in.from == SERVER_ID, "chat: server is id 0");
			CHECK(in.text == "Welcome to the Commonwealth.", "chat: text");
		}

		{
			const auto bytes = Encode(PlayerRenamedMsg{ 3, "Nick Valentine" });
			Reader r(bytes.data(), bytes.size());
			MsgId id{};
			(void)ReadHeader(r, id);
			PlayerRenamedMsg in;
			CHECK(id == MsgId::kPlayerRenamed && in.Read(r) && in.id == 3 && in.name == "Nick Valentine", "PlayerRenamed round-trips");
		}

		// A message body cut short must fail cleanly.
		auto bytes = Encode(PlayerJoinedMsg{ 1, "Akira" });
		bytes.resize(bytes.size() - 2);
		Reader r(bytes.data(), bytes.size());
		MsgId id{};
		(void)ReadHeader(r, id);
		PlayerJoinedMsg partial;
		CHECK(!partial.Read(r), "a truncated message is rejected");
	}

	void TestSanitizeName()
	{
		Suite("Name sanitising");

		CHECK(SanitizeName("Akira") == "Akira", "a plain name is untouched");
		CHECK(SanitizeName("  Akira  ") == "Akira", "surrounding spaces are trimmed");
		CHECK(SanitizeName("") == "Wastelander", "an empty name gets the default");
		CHECK(SanitizeName("   ") == "Wastelander", "a whitespace-only name gets the default");
		CHECK(SanitizeName("Aki\nra\t!") == "Akira!", "control characters are stripped");
		CHECK(SanitizeName(std::string(200, 'x')).size() == MAX_PLAYER_NAME, "long names are clamped");
		CHECK(SanitizeName("\x01\x02") == "Wastelander", "a name of only control characters gets the default");
	}

	void TestIni()
	{
		Suite("INI parsing");

		const auto path = std::filesystem::temp_directory_path() / "f4mp_test.ini";
		{
			std::ofstream file(path);
			file << "; a comment\n"
				 << "# another\n"
				 << "[Network]\n"
				 << "Host = 10.0.0.5\n"
				 << "Port=27016\n"
				 << "AutoConnect = yes\n"
				 << "InterpDelayMs = 62.5\n"
				 << "SendRate = 20 ; per second\n"
				 << "Typo = 270I5\n"
				 << "Negative = -5\n"
				 << "\n"
				 << "[player]\n"
				 << "Name=Akira\n"
				 << "Motto=hello; goodbye\n";
		}

		Ini ini;
		CHECK(ini.Load(path), "the file loads");
		CHECK(ini.Get("Network", "Host") == "10.0.0.5", "values are trimmed around '='");
		CHECK(ini.GetInt("Network", "Port", 0) == 27016, "integers parse");
		CHECK(ini.GetBool("Network", "AutoConnect", false), "\"yes\" is true");
		CHECK(Near(ini.GetFloat("Network", "InterpDelayMs", 0.0), 62.5), "floats parse");
		CHECK(ini.Get("PLAYER", "name") == "Akira", "sections and keys are case-insensitive");
		CHECK(ini.Get("Network", "Missing", "fallback") == "fallback", "a missing key falls back");
		CHECK(ini.GetInt("Network", "Host", 42) == 42, "a dotted address is not read as an integer");
		CHECK(ini.GetInt("Network", "Typo", 20) == 20, "a typo'd number falls back instead of truncating");
		CHECK(ini.GetInt("Network", "SendRate", 0) == 20, "an inline comment after a number is ignored");
		CHECK(ini.GetInt("Network", "Negative", 0) == -5, "negative integers parse");
		CHECK(ini.Get("Player", "Motto") == "hello; goodbye", "a semicolon inside a string value is kept");

		Ini missing;
		CHECK(!missing.Load(path.parent_path() / "f4mp_does_not_exist.ini"), "a missing file reports failure");
		std::filesystem::remove(path);
	}

	void TestServerClock()
	{
		Suite("Server clock unwrapping");

		ServerClock clock;
		CHECK(!clock.Started(), "starts unset");
		CHECK(clock.Unwrap(1000) == 1000, "the first stamp seeds the timeline");
		CHECK(clock.Unwrap(1050) == 1050, "a normal step advances");
		CHECK(clock.Unwrap(4000) == 4000, "a larger step advances");

		// The u32 stamp wraps after ~49.7 days of uptime; the timeline must not jump backwards.
		ServerClock wrapping;
		const std::uint32_t nearMax = 0xFFFFFF00u;
		const auto before = wrapping.Unwrap(nearMax);
		const auto after = wrapping.Unwrap(0x00000100u); // 512 ms later, having wrapped
		CHECK(after - before == 512, "a wrap is absorbed as a 512 ms step");
		CHECK(after > before, "the timeline stays monotonic across the wrap");

		wrapping.Reset();
		CHECK(!wrapping.Started(), "reset clears it");
	}

	TimedState Stamp(float a_x, std::int64_t a_ms, float a_yaw = 0.0f)
	{
		TimedState t;
		t.state.position = Vec3{ a_x, 0.0f, 0.0f };
		t.state.yaw = a_yaw;
		t.timeMs = a_ms;
		return t;
	}

	void TestInterpolation()
	{
		Suite("Snapshot interpolation");

		{
			const std::vector<TimedState> one{ Stamp(10.0f, 1000) };
			CHECK(SampleAt(one, 5000.0).position.x == 10.0f, "a single snapshot is held");
		}

		const std::vector<TimedState> snaps{ Stamp(0.0f, 1000), Stamp(100.0f, 1100), Stamp(200.0f, 1200) };

		CHECK(SampleAt(snaps, 500.0).position.x == 0.0f, "before the oldest, the oldest is held");
		CHECK(Near(SampleAt(snaps, 1050.0).position.x, 50.0), "halfway between two snapshots interpolates");
		CHECK(Near(SampleAt(snaps, 1100.0).position.x, 100.0), "exactly on a snapshot returns it");
		CHECK(Near(SampleAt(snaps, 1175.0).position.x, 175.0), "the second interval interpolates too");
		CHECK(Near(SampleAt(snaps, 1200.0).position.x, 200.0), "exactly on the newest returns it");

		// Past the newest: carry the last velocity, but only briefly.
		CHECK(Near(SampleAt(snaps, 1250.0).position.x, 250.0), "just past the newest extrapolates");
		CHECK(Near(SampleAt(snaps, 1350.0).position.x, 350.0), "extrapolation is capped at 150 ms");
		CHECK(Near(SampleAt(snaps, 5000.0).position.x, 350.0), "far past the newest holds the capped value");

		// A long gap means the player stood still and only the heartbeat arrived: no velocity.
		const std::vector<TimedState> stale{ Stamp(0.0f, 1000), Stamp(100.0f, 2000) };
		CHECK(Near(SampleAt(stale, 2100.0).position.x, 100.0), "a stale pair is not extrapolated");

		// Yaw takes the short way round: 350 degrees to 10 degrees crosses zero, not backwards.
		constexpr float deg = std::numbers::pi_v<float> / 180.0f;
		const std::vector<TimedState> turning{ Stamp(0.0f, 1000, 350.0f * deg), Stamp(0.0f, 1100, 10.0f * deg) };
		const auto mid = SampleAt(turning, 1050.0).yaw;
		const auto midDeg = mid / deg;
		CHECK_MSG(Near(midDeg, 360.0, 0.5) || Near(midDeg, 0.0, 0.5), "yaw wraps the short way",
			"got " + std::to_string(midDeg) + " degrees");

		// Discrete fields must never be averaged into something that was never true.
		std::vector<TimedState> flagged{ Stamp(0.0f, 1000), Stamp(100.0f, 1100) };
		flagged[0].state.flags = kStateNone;
		flagged[0].state.cellId = 0x11;
		flagged[1].state.flags = kStateSneaking;
		flagged[1].state.cellId = 0x22;
		CHECK(SampleAt(flagged, 1010.0).flags == kStateNone, "flags follow the nearer snapshot (early)");
		CHECK(SampleAt(flagged, 1090.0).flags == kStateSneaking, "flags follow the nearer snapshot (late)");
		CHECK(SampleAt(flagged, 1090.0).cellId == 0x22u, "cell id is never blended");

		// Two snapshots with the same stamp must not divide by zero.
		const std::vector<TimedState> duplicated{ Stamp(0.0f, 1000), Stamp(100.0f, 1000) };
		const auto sample = SampleAt(duplicated, 1000.0);
		CHECK(std::isfinite(sample.position.x), "duplicate timestamps do not produce NaN");
	}
}

int main()
{
	std::printf("F4MP unit tests (protocol v%u)\n", PROTOCOL_VERSION);
	TestPacket();
	TestMessages();
	TestSanitizeName();
	TestIni();
	TestServerClock();
	TestInterpolation();
	return Summary();
}
