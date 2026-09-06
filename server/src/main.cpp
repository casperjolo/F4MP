#include "Config.hpp"
#include "Console.hpp"
#include "Server.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{
	std::atomic<bool> g_running{ true };

	void OnSignal(int)
	{
		g_running = false;
	}

	void SetupLogging(const f4mp::server::Config& a_config)
	{
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		if (!a_config.logFile.empty()) {
			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(a_config.logFile, false));
		}

		auto logger = std::make_shared<spdlog::logger>("f4mp", sinks.begin(), sinks.end());
		logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		logger->set_level(spdlog::level::from_str(a_config.logLevel));
		logger->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(logger));
	}
}

int main(int argc, char** argv)
{
	std::filesystem::path configPath = "server.ini";
	std::optional<std::uint16_t> portOverride;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
			configPath = argv[++i];
		} else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
			portOverride = static_cast<std::uint16_t>(std::stoul(argv[++i]));
		} else if (arg == "--help" || arg == "-h") {
			std::printf("F4MPServer [--config server.ini] [--port 27015]\n");
			return 0;
		}
	}

	f4mp::server::Config config;
	const auto loaded = config.Load(configPath);
	if (portOverride) {
		config.port = *portOverride;
	}

	SetupLogging(config);
	spdlog::info("F4MP dedicated server v{}", F4MP_VERSION_STRING);
	if (!loaded) {
		spdlog::warn("Could not read {}, using defaults", configPath.string());
	}

	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);

	f4mp::server::Server server(config);
	if (!server.Start()) {
		return 1;
	}

	f4mp::server::Console console;
	const auto tickMs = std::max<std::uint32_t>(1, 1000 / config.tickRate);

	spdlog::info("Type help for commands, stop to shut down");
	while (g_running) {
		server.Service(tickMs);

		while (auto line = console.TryPop()) {
			if (*line == "stop" || *line == "quit" || *line == "exit") {
				g_running = false;
				break;
			}
			server.ExecuteCommand(*line);
		}
	}

	server.Stop();
	spdlog::shutdown();
	return 0;
}
