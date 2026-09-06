#include "Console.hpp"

#include <iostream>

namespace f4mp::server
{
	Console::Console()
		: _thread(&Console::ReadLoop, this)
	{
		// stdin has no portable non-blocking mode; the reader thread is detached
		// and simply dies with the process.
		_thread.detach();
	}

	Console::~Console() = default;

	std::optional<std::string> Console::TryPop()
	{
		const std::scoped_lock lock(_mutex);
		if (_lines.empty()) {
			return std::nullopt;
		}
		auto line = std::move(_lines.front());
		_lines.pop();
		return line;
	}

	void Console::ReadLoop()
	{
		std::string line;
		while (std::getline(std::cin, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const std::scoped_lock lock(_mutex);
			_lines.push(line);
		}
	}
}
