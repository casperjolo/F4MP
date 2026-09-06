#pragma once

namespace f4mp::client
{
	// Reads the local player's state out of the engine.
	class LocalPlayer
	{
	public:
		// Returns false while the player is not in the world yet.
		[[nodiscard]] bool Sample(PlayerState& a_out) const;
	};
}
