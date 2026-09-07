#pragma once

#include "f4mp/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

// Snapshot interpolation shared by the client and the tests. Pure maths: no engine types, so it
// can be exercised headless. See docs/PROTOCOL.md ("Timing").
namespace f4mp
{
	// Beyond the newest snapshot the position is carried forward for at most this long.
	inline constexpr double MAX_EXTRAPOLATE_MS = 150.0;
	// Snapshot pairs further apart than this carry no useful velocity (the player probably stood
	// still and only the 1 Hz heartbeat arrived).
	inline constexpr double MAX_VELOCITY_GAP_MS = 400.0;

	// A snapshot on the client's unwrapped server timeline (see ServerClock).
	struct TimedState
	{
		PlayerState state{};
		std::int64_t timeMs{ 0 };
	};

	// Turns the u32 millisecond stamp on the wire into a monotonic timeline. The stamp wraps
	// every ~49.7 days of server uptime; the wrap is absorbed by taking signed deltas.
	class ServerClock
	{
	public:
		[[nodiscard]] std::int64_t Unwrap(std::uint32_t a_stamp) noexcept
		{
			if (!_started) {
				_started = true;
				_last = a_stamp;
				_now = static_cast<std::int64_t>(a_stamp);
				return _now;
			}
			_now += static_cast<std::int32_t>(a_stamp - _last);
			_last = a_stamp;
			return _now;
		}

		void Reset() noexcept { *this = ServerClock{}; }

		[[nodiscard]] bool Started() const noexcept { return _started; }

	private:
		bool _started{ false };
		std::uint32_t _last{ 0 };
		std::int64_t _now{ 0 };
	};

	[[nodiscard]] inline float Lerp(float a_a, float a_b, float a_t) noexcept
	{
		return a_a + (a_b - a_a) * a_t;
	}

	// Interpolates along the shortest arc so yaw does not spin through 360 degrees.
	[[nodiscard]] inline float LerpAngle(float a_a, float a_b, float a_t) noexcept
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

	// Continuous fields are blended; discrete ones (cell, flags) follow the nearer snapshot.
	[[nodiscard]] inline PlayerState Blend(const PlayerState& a_a, const PlayerState& a_b, float a_t)
	{
		PlayerState out = a_t < 0.5f ? a_a : a_b;
		out.position.x = Lerp(a_a.position.x, a_b.position.x, a_t);
		out.position.y = Lerp(a_a.position.y, a_b.position.y, a_t);
		out.position.z = Lerp(a_a.position.z, a_b.position.z, a_t);
		out.yaw = LerpAngle(a_a.yaw, a_b.yaw, a_t);
		out.health = Lerp(a_a.health, a_b.health, a_t);
		return out;
	}

	// Where a_snapshots (oldest first, never empty) says the player was at a_renderMs.
	// Before the oldest: hold the oldest. Between two: interpolate. After the newest: carry the
	// last velocity for up to MAX_EXTRAPOLATE_MS, then hold.
	[[nodiscard]] inline PlayerState SampleAt(std::span<const TimedState> a_snapshots, double a_renderMs)
	{
		const auto& first = a_snapshots.front();
		const auto& last = a_snapshots.back();

		if (a_snapshots.size() == 1 || a_renderMs <= static_cast<double>(first.timeMs)) {
			return first.state;
		}

		if (a_renderMs >= static_cast<double>(last.timeMs)) {
			const auto& prev = a_snapshots[a_snapshots.size() - 2];
			const double gap = static_cast<double>(last.timeMs - prev.timeMs);
			const double ahead = std::min(a_renderMs - static_cast<double>(last.timeMs), MAX_EXTRAPOLATE_MS);
			if (gap <= 0.0 || gap > MAX_VELOCITY_GAP_MS || ahead <= 0.0) {
				return last.state;
			}
			const auto t = static_cast<float>(ahead / gap);
			PlayerState out = last.state;
			out.position.x += (last.state.position.x - prev.state.position.x) * t;
			out.position.y += (last.state.position.y - prev.state.position.y) * t;
			out.position.z += (last.state.position.z - prev.state.position.z) * t;
			return out;
		}

		for (std::size_t i = 0; i + 1 < a_snapshots.size(); ++i) {
			const auto& a = a_snapshots[i];
			const auto& b = a_snapshots[i + 1];
			if (a_renderMs < static_cast<double>(a.timeMs) || a_renderMs > static_cast<double>(b.timeMs)) {
				continue;
			}
			const double span = static_cast<double>(b.timeMs - a.timeMs);
			const auto t = span > 0.0 ? static_cast<float>((a_renderMs - static_cast<double>(a.timeMs)) / span) : 1.0f;
			return Blend(a.state, b.state, std::clamp(t, 0.0f, 1.0f));
		}
		return last.state;
	}
}
