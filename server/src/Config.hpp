#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace f4mp::server
{
	struct Config
	{
		std::string name{ "F4MP Server" };
		std::string motd{ "Welcome to the Commonwealth." };
		std::uint16_t port{ 27015 };
		std::uint32_t maxPlayers{ 16 };
		std::uint32_t tickRate{ 30 };
		std::string logFile{ "server.log" };
		std::string logLevel{ "info" };

		// Returns false if the file could not be read (defaults are kept).
		bool Load(const std::filesystem::path& path);
	};
}
