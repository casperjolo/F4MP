#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

// Tiny dependency-free INI reader: [Section] / Key=Value / comments starting with ; or #.
namespace f4mp
{
	class Ini
	{
	public:
		bool Load(const std::filesystem::path& path)
		{
			std::ifstream file(path);
			if (!file) {
				return false;
			}

			std::string section;
			std::string line;
			while (std::getline(file, line)) {
				auto view = Trim(line);
				if (view.empty() || view.front() == ';' || view.front() == '#') {
					continue;
				}
				if (view.front() == '[' && view.back() == ']') {
					section = Lower(view.substr(1, view.size() - 2));
					continue;
				}
				const auto eq = view.find('=');
				if (eq == std::string_view::npos) {
					continue;
				}
				auto key = Lower(Trim(view.substr(0, eq)));
				auto value = std::string(Trim(view.substr(eq + 1)));
				_values[section + "/" + key] = std::move(value);
			}
			return true;
		}

		[[nodiscard]] std::string Get(std::string_view section, std::string_view key, std::string_view fallback = {}) const
		{
			const auto it = _values.find(Lower(section) + "/" + Lower(key));
			return it == _values.end() ? std::string(fallback) : it->second;
		}

		// Numbers must be the whole value: "Port=270I5" falls back rather than silently
		// becoming 270. A trailing "; comment" is allowed, since a number never contains one.
		[[nodiscard]] long long GetInt(std::string_view section, std::string_view key, long long fallback) const
		{
			const auto raw = Get(section, key); // owns the storage `s` points into
			const auto s = Numeric(raw);
			long long v{};
			const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
			return res.ec == std::errc{} && res.ptr == s.data() + s.size() ? v : fallback;
		}

		[[nodiscard]] double GetFloat(std::string_view section, std::string_view key, double fallback) const
		{
			const auto raw = Get(section, key);
			const auto s = Numeric(raw);
			double v{};
			const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
			return res.ec == std::errc{} && res.ptr == s.data() + s.size() ? v : fallback;
		}

		[[nodiscard]] bool GetBool(std::string_view section, std::string_view key, bool fallback) const
		{
			const auto raw = Get(section, key);
			const auto s = Lower(Numeric(raw));
			if (s == "1" || s == "true" || s == "yes" || s == "on") {
				return true;
			}
			if (s == "0" || s == "false" || s == "no" || s == "off") {
				return false;
			}
			return fallback;
		}

	private:
		// A value about to be parsed as a number or a boolean: drop any inline comment and the
		// space around what is left. Never used for plain string values, which may contain ';'.
		static std::string_view Numeric(std::string_view s)
		{
			const auto comment = s.find_first_of(";#");
			if (comment != std::string_view::npos) {
				s = s.substr(0, comment);
			}
			return Trim(s);
		}

		static std::string_view Trim(std::string_view s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
				s.remove_prefix(1);
			}
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
				s.remove_suffix(1);
			}
			return s;
		}

		static std::string Lower(std::string_view s)
		{
			std::string out(s);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::unordered_map<std::string, std::string> _values;
	};
}
