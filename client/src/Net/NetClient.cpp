#include "PCH.hpp"

#include "Net/NetClient.hpp"

#include <enet/enet.h>

namespace f4mp::client
{
	NetClient::NetClient()
	{
		if (enet_initialize() == 0) {
			_enetReady = true;
		} else {
			REX::LogError("enet_initialize() failed; networking disabled"sv);
		}
	}

	NetClient::~NetClient()
	{
		Reset();
		if (_host) {
			enet_host_destroy(_host);
			_host = nullptr;
		}
		if (_enetReady) {
			enet_deinitialize();
		}
	}

	bool NetClient::Connect(const std::string& a_host, std::uint16_t a_port)
	{
		if (!_enetReady) {
			return false;
		}

		Reset();

		if (!_host) {
			_host = enet_host_create(nullptr, 1, static_cast<std::size_t>(Channel::kCount), 0, 0);
			if (!_host) {
				REX::LogError("enet_host_create() failed"sv);
				return false;
			}
		}

		ENetAddress address{};
		if (enet_address_set_host(&address, a_host.c_str()) != 0) {
			REX::LogError("Could not resolve host \"{}\""sv, a_host);
			return false;
		}
		address.port = a_port;

		_peer = enet_host_connect(_host, &address, static_cast<std::size_t>(Channel::kCount), 0);
		if (!_peer) {
			REX::LogError("enet_host_connect() failed"sv);
			return false;
		}

		_state = State::kConnecting;
		_connectStart = std::chrono::steady_clock::now();
		REX::LogInformation("Connecting to {}:{}"sv, a_host, a_port);
		return true;
	}

	void NetClient::Disconnect()
	{
		if (_state == State::kDisconnected) {
			return;
		}
		Reset();
		if (onDisconnected) {
			onDisconnected("Disconnected");
		}
	}

	void NetClient::Reset()
	{
		if (_peer) {
			if (_state == State::kConnected) {
				enet_peer_disconnect_now(_peer, 0);
			} else {
				enet_peer_reset(_peer);
			}
			_peer = nullptr;
		}
		_state = State::kDisconnected;
	}

	void NetClient::Fail(const std::string& a_reason)
	{
		REX::LogWarning("Connection failed: {}"sv, a_reason);
		Reset();
		if (onDisconnected) {
			onDisconnected(a_reason);
		}
	}

	void NetClient::Pump()
	{
		if (!_host) {
			return;
		}

		ENetEvent event{};
		while (enet_host_service(_host, &event, 0) > 0) {
			switch (event.type) {
			case ENET_EVENT_TYPE_CONNECT:
				_state = State::kConnected;
				REX::LogInformation("Connected"sv);
				if (onConnected) {
					onConnected();
				}
				break;

			case ENET_EVENT_TYPE_RECEIVE:
				if (onPacket) {
					onPacket(std::span<const std::uint8_t>(event.packet->data, event.packet->dataLength));
				}
				enet_packet_destroy(event.packet);
				break;

			case ENET_EVENT_TYPE_DISCONNECT: {
				// ENet has already released the peer slot.
				const auto wasConnecting = _state == State::kConnecting;
				_peer = nullptr;
				_state = State::kDisconnected;
				const std::string reason = wasConnecting ? "Connection refused" : "Connection closed";
				REX::LogInformation("{}"sv, reason);
				if (onDisconnected) {
					onDisconnected(reason);
				}
				break;
			}

			default:
				break;
			}
		}

		if (_state == State::kConnecting && std::chrono::steady_clock::now() - _connectStart > CONNECT_TIMEOUT) {
			Fail("Connection timed out");
		}
	}

	void NetClient::Send(Channel a_channel, const std::vector<std::uint8_t>& a_bytes)
	{
		if (_state != State::kConnected || !_peer) {
			return;
		}

		const enet_uint32 flags = a_channel == Channel::kReliable ? ENET_PACKET_FLAG_RELIABLE : 0u;
		auto* packet = enet_packet_create(a_bytes.data(), a_bytes.size(), flags);
		if (packet) {
			enet_peer_send(_peer, static_cast<enet_uint8>(a_channel), packet);
		}
	}

	std::uint32_t NetClient::GetPingMs() const
	{
		return _peer ? _peer->roundTripTime : 0;
	}
}
