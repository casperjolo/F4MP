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

		// [NewGame] Skip the pre-war prologue and start at the Vault 111 cryo pod.
		bool skipPrologue{ true };
		float skipDelay{ 2.0f };
		float podTimeout{ 120.0f };
		std::vector<std::string> skipCommands{
			"setstage MQ101 900", // 2287: teleports into the vault, skips the Kellogg scene, the pod opens
			"removemusic 1A8676", // pre-war music
		};
		bool chargen{ true };
		std::int32_t chargenMode{ 0 }; // 0: ask for the sex, then face; 1: face only

		// [Sync] NPC record remote players' clones are copied from.
		RE::TESFormID cloneBase{ 0x00000007 };

		std::filesystem::path path;

		// Reads Data/F4SE/Plugins/F4MP.ini; missing file or keys fall back to defaults.
		[[nodiscard]] static Config Load();
	};
}
