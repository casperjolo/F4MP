#pragma once

#include "f4mp/Packet.hpp"
#include "f4mp/Types.hpp"

#include <string>
#include <vector>

// Wire protocol shared by the F4SE client plugin and the dedicated server.
// Bump PROTOCOL_VERSION whenever a message layout changes. See docs/PROTOCOL.md.
namespace f4mp
{
	inline constexpr std::uint32_t PROTOCOL_VERSION = 1;
	inline constexpr std::uint16_t DEFAULT_PORT = 27015;
	inline constexpr std::size_t MAX_PLAYER_NAME = 32;
	inline constexpr std::size_t MAX_CHAT_BYTES = 512; // longer chat lines are cut, not rejected

	enum class Channel : std::uint8_t
	{
		kReliable = 0,   // handshake, join/leave, chat
		kUnreliable = 1, // high-frequency state (sequenced, may drop)

		kCount
	};

	enum class MsgId : std::uint8_t
	{
		// client -> server
		kHello = 1,
		kPlayerState = 2,
		kChat = 3,

		// server -> client
		kWelcome = 64,
		kReject = 65,
		kPlayerJoined = 66,
		kPlayerLeft = 67,
		kPlayerStateUpdate = 68,
		kChatBroadcast = 69,
	};

	inline void WriteVec3(Writer& w, const Vec3& v)
	{
		w.F32(v.x);
		w.F32(v.y);
		w.F32(v.z);
	}

	inline Vec3 ReadVec3(Reader& r)
	{
		Vec3 v;
		v.x = r.F32();
		v.y = r.F32();
		v.z = r.F32();
		return v;
	}

	inline void WritePlayerState(Writer& w, const PlayerState& s)
	{
		WriteVec3(w, s.position);
		w.F32(s.yaw);
		w.U32(s.worldspaceId);
		w.U32(s.cellId);
		w.U8(s.flags);
		w.F32(s.health);
		w.F32(s.maxHealth);
	}

	inline PlayerState ReadPlayerState(Reader& r)
	{
		PlayerState s;
		s.position = ReadVec3(r);
		s.yaw = r.F32();
		s.worldspaceId = r.U32();
		s.cellId = r.U32();
		s.flags = r.U8();
		s.health = r.F32();
		s.maxHealth = r.F32();
		return s;
	}

	// ---- client -> server -------------------------------------------------

	struct HelloMsg
	{
		static constexpr auto ID = MsgId::kHello;

		std::uint32_t protocolVersion{ PROTOCOL_VERSION };
		std::uint32_t gameVersion{ 0 }; // packed Fallout4.exe version (major<<24 | minor<<16 | build)
		std::string name;

		void Write(Writer& w) const
		{
			w.U32(protocolVersion);
			w.U32(gameVersion);
			w.Str(name);
		}

		bool Read(Reader& r)
		{
			protocolVersion = r.U32();
			gameVersion = r.U32();
			name = r.Str();
			return r.Ok();
		}
	};

	struct PlayerStateMsg
	{
		static constexpr auto ID = MsgId::kPlayerState;

		PlayerState state;

		void Write(Writer& w) const { WritePlayerState(w, state); }

		bool Read(Reader& r)
		{
			state = ReadPlayerState(r);
			return r.Ok();
		}
	};

	struct ChatMsg
	{
		static constexpr auto ID = MsgId::kChat;

		std::string text;

		void Write(Writer& w) const { w.Str(text); }

		bool Read(Reader& r)
		{
			text = r.Str();
			return r.Ok();
		}
	};

	// ---- server -> client -------------------------------------------------

	struct WelcomeMsg
	{
		static constexpr auto ID = MsgId::kWelcome;

		PlayerId id{ INVALID_PLAYER_ID };
		std::uint8_t tickRate{ 20 };
		std::string serverName;
		std::string motd;

		void Write(Writer& w) const
		{
			w.U32(id);
			w.U8(tickRate);
			w.Str(serverName);
			w.Str(motd);
		}

		bool Read(Reader& r)
		{
			id = r.U32();
			tickRate = r.U8();
			serverName = r.Str();
			motd = r.Str();
			return r.Ok();
		}
	};

	struct RejectMsg
	{
		static constexpr auto ID = MsgId::kReject;

		std::string reason;

		void Write(Writer& w) const { w.Str(reason); }

		bool Read(Reader& r)
		{
			reason = r.Str();
			return r.Ok();
		}
	};

	struct PlayerJoinedMsg
	{
		static constexpr auto ID = MsgId::kPlayerJoined;

		PlayerId id{ INVALID_PLAYER_ID };
		std::string name;

		void Write(Writer& w) const
		{
			w.U32(id);
			w.Str(name);
		}

		bool Read(Reader& r)
		{
			id = r.U32();
			name = r.Str();
			return r.Ok();
		}
	};

	struct PlayerLeftMsg
	{
		static constexpr auto ID = MsgId::kPlayerLeft;

		PlayerId id{ INVALID_PLAYER_ID };

		void Write(Writer& w) const { w.U32(id); }

		bool Read(Reader& r)
		{
			id = r.U32();
			return r.Ok();
		}
	};

	struct PlayerStateUpdateMsg
	{
		static constexpr auto ID = MsgId::kPlayerStateUpdate;

		PlayerId id{ INVALID_PLAYER_ID };
		PlayerState state;

		void Write(Writer& w) const
		{
			w.U32(id);
			WritePlayerState(w, state);
		}

		bool Read(Reader& r)
		{
			id = r.U32();
			state = ReadPlayerState(r);
			return r.Ok();
		}
	};

	struct ChatBroadcastMsg
	{
		static constexpr auto ID = MsgId::kChatBroadcast;

		PlayerId from{ SERVER_ID }; // SERVER_ID for server messages
		std::string text;

		void Write(Writer& w) const
		{
			w.U32(from);
			w.Str(text);
		}

		bool Read(Reader& r)
		{
			from = r.U32();
			text = r.Str();
			return r.Ok();
		}
	};

	// ---- helpers ----------------------------------------------------------

	template <class T>
	[[nodiscard]] std::vector<std::uint8_t> Encode(const T& msg)
	{
		Writer w;
		w.U8(static_cast<std::uint8_t>(T::ID));
		msg.Write(w);
		return w.Take();
	}

	// Reads the message id and leaves the reader positioned at the payload.
	[[nodiscard]] inline bool ReadHeader(Reader& r, MsgId& id)
	{
		id = static_cast<MsgId>(r.U8());
		return r.Ok();
	}

	// Strips control characters, trims, clamps length, never returns an empty name.
	[[nodiscard]] inline std::string SanitizeName(const std::string& name)
	{
		std::string out;
		for (const char c : name) {
			const auto uc = static_cast<unsigned char>(c);
			if (uc >= 0x20 && uc < 0x7F) {
				out.push_back(c);
			}
		}
		if (out.size() > MAX_PLAYER_NAME) {
			out.resize(MAX_PLAYER_NAME);
		}
		while (!out.empty() && out.back() == ' ') {
			out.pop_back();
		}
		while (!out.empty() && out.front() == ' ') {
			out.erase(out.begin());
		}
		return out.empty() ? std::string{ "Wastelander" } : out;
	}
}
