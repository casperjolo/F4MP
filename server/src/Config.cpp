#include "Config.hpp"

#include "f4mp/Ini.hpp"

#include <algorithm>

namespace f4mp::server
{
	bool Config::Load(const std::filesystem::path& path)
	{
		Ini ini;
		if (!ini.Load(path)) {
			return false;
		}

		name = ini.Get("Server", "Name", name);
		motd = ini.Get("Server", "Motd", motd);
		port = static_cast<std::uint16_t>(std::clamp<long long>(ini.GetInt("Server", "Port", port), 1, 65535));
		maxPlayers = static_cast<std::uint32_t>(std::clamp<long long>(ini.GetInt("Server", "MaxPlayers", maxPlayers), 1, 4095));
		tickRate = static_cast<std::uint32_t>(std::clamp<long long>(ini.GetInt("Server", "TickRate", tickRate), 1, 240));
		logFile = ini.Get("Logging", "File", logFile);
		logLevel = ini.Get("Logging", "Level", logLevel);
		return true;
	}
}
