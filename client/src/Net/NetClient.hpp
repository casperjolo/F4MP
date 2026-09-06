#pragma once

struct _ENetHost;
struct _ENetPeer;

namespace f4mp::client
{
	// ENet client connection. Everything runs on the game thread; call Pump() once per frame.
	class NetClient
	{
	public:
		enum class State
		{
			kDisconnected,
			kConnecting,
			kConnected
		};

		NetClient();
		~NetClient();

		NetClient(const NetClient&) = delete;
		NetClient& operator=(const NetClient&) = delete;

		bool Connect(const std::string& a_host, std::uint16_t a_port);
		void Disconnect();

		// Processes incoming packets and connection events; invokes the callbacks below.
		void Pump();

		void Send(Channel a_channel, const std::vector<std::uint8_t>& a_bytes);

		[[nodiscard]] State GetState() const noexcept { return _state; }
		[[nodiscard]] bool IsConnected() const noexcept { return _state == State::kConnected; }
		[[nodiscard]] std::uint32_t GetPingMs() const;

		std::function<void()> onConnected;
		std::function<void(const std::string& a_reason)> onDisconnected;
		std::function<void(std::span<const std::uint8_t> a_bytes)> onPacket;

	private:
		void Reset();
		void Fail(const std::string& a_reason);

		static constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(8);

		_ENetHost* _host{ nullptr };
		_ENetPeer* _peer{ nullptr };
		State _state{ State::kDisconnected };
		bool _enetReady{ false };
		std::chrono::steady_clock::time_point _connectStart{};
	};
}
