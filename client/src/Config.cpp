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

		cfg.skipPrologue = ini.GetBool("NewGame", "SkipPrologue", cfg.skipPrologue);
		cfg.skipDelay = static_cast<float>(std::clamp(ini.GetFloat("NewGame", "SkipDelay", cfg.skipDelay), 0.0, 60.0));
		cfg.podTimeout = static_cast<float>(std::clamp(ini.GetFloat("NewGame", "PodTimeout", cfg.podTimeout), 5.0, 600.0));
		cfg.chargen = ini.GetBool("NewGame", "Chargen", cfg.chargen);
		cfg.chargenMode = static_cast<std::int32_t>(std::clamp<long long>(ini.GetInt("NewGame", "ChargenMode", cfg.chargenMode), 0, 1));

		// SkipCommands=cmd; cmd; wait 3; cmd  -- an explicit empty value means "no commands".
		if (const auto list = ini.Get("NewGame", "SkipCommands", "\x01"); list != "\x01") {
			cfg.skipCommands.clear();
			std::size_t start = 0;
			while (start <= list.size()) {
				auto end = list.find(';', start);
				if (end == std::string::npos) {
					end = list.size();
				}
				auto item = list.substr(start, end - start);
				const auto first = item.find_first_not_of(" \t");
				const auto last = item.find_last_not_of(" \t");
				if (first != std::string::npos) {
					cfg.skipCommands.push_back(item.substr(first, last - first + 1));
				}
				start = end + 1;
			}
		}

		REX::LogInformation("Config: host={} port={} autoConnect={} sendRate={} interpDelay={}ms name=\"{}\""sv,
			cfg.host, cfg.port, cfg.autoConnect, cfg.sendRate, cfg.interpDelayMs, cfg.playerName);
		REX::LogInformation("Config: skipPrologue={} skipDelay={}s podTimeout={}s chargen={} chargenMode={} skipCommands={}"sv,
			cfg.skipPrologue, cfg.skipDelay, cfg.podTimeout, cfg.chargen, cfg.chargenMode, cfg.skipCommands.size());
		return cfg;
	}
}
