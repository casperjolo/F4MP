#include "PCH.hpp"

#include "Config.hpp"

namespace f4mp::client
{
	Config Config::Load()
	{
		Config cfg;
		cfg.path = std::filesystem::path(std::string(F4SE::PLUGINS_DIRECTORY_PATH)) / "F4MP.ini";

		Ini ini;
		if (!ini.Load(cfg.path)) {
			REX::LogWarning("{} not found, using defaults"sv, cfg.path.string());
			return cfg;
		}

		cfg.host = ini.Get("Network", "Host", cfg.host);
		cfg.port = static_cast<std::uint16_t>(std::clamp<long long>(ini.GetInt("Network", "Port", cfg.port), 1, 65535));
		cfg.autoConnect = ini.GetBool("Network", "AutoConnect", cfg.autoConnect);
		cfg.sendRate = static_cast<std::uint32_t>(std::clamp<long long>(ini.GetInt("Network", "SendRate", cfg.sendRate), 1, 60));
		cfg.interpDelayMs = static_cast<float>(std::clamp(ini.GetFloat("Network", "InterpDelayMs", cfg.interpDelayMs), 0.0, 1000.0));
		cfg.playerName = ini.Get("Player", "Name", cfg.playerName);
		cfg.consoleMessages = ini.GetBool("Debug", "ConsoleMessages", cfg.consoleMessages);

		REX::LogInformation("Config: host={} port={} autoConnect={} sendRate={} interpDelay={}ms name=\"{}\""sv,
			cfg.host, cfg.port, cfg.autoConnect, cfg.sendRate, cfg.interpDelayMs, cfg.playerName);
		return cfg;
	}
}
