#pragma once

#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace f4mp::server
{
	// Reads stdin on a background thread and hands complete lines to the main loop.
	class Console
	{
	public:
		Console();
		~Console();

		Console(const Console&) = delete;
		Console& operator=(const Console&) = delete;

		[[nodiscard]] std::optional<std::string> TryPop();

	private:
		void ReadLoop();

		std::mutex _mutex;
		std::queue<std::string> _lines;
		std::thread _thread;
	};
}
