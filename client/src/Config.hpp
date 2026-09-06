#pragma once

namespace f4mp::client
{
	struct Config
	{
		std::string host{ "127.0.0.1" };
		std::uint16_t port{ DEFAULT_PORT };
		bool autoConnect{ true };
		std::uint32_t sendRate{ 20 };
		float interpDelayMs{ 100.0f };
		std::string playerName; // empty: use the character name
		bool consoleMessages{ true };

		std::filesystem::path path;

		// Reads Data/F4SE/Plugins/F4MP.ini; missing file or keys fall back to defaults.
		[[nodiscard]] static Config Load();
	};
}
