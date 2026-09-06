#include "PCH.hpp"

#include "Game/ConsoleCommands.hpp"

#include "Game/GameUtil.hpp"
#include "Session.hpp"

#include <cctype>
#include <charconv>

namespace f4mp::client::console
{
	namespace
	{
		// A stock debug command nobody uses in play; its table entry becomes "f4mp".
		constexpr auto HIJACKED_COMMAND = "TestSeenData"sv;
		constexpr auto COMMAND_NAME = "F4MP";
		constexpr auto COMMAND_SHORT = "f4mp";
		constexpr auto COMMAND_HELP = "F4MP multiplayer. Usage: f4mp help | status | connect [host[:port]] | disconnect | say <text> | list | name <name> | unstick";

		// One optional string parameter keeps the script compiler happy with "f4mp status".
		// The real argument line is read back from the script text so that
		// "f4mp say hello there" works without quotes.
		RE::SCRIPT_PARAMETER PARAMETERS[] = {
			{ .paramName = "Subcommand", .paramType = RE::SCRIPT_PARAM_TYPE::kChar, .optional = true },
		};

		bool IsSpace(char a_c) noexcept
		{
			return std::isspace(static_cast<unsigned char>(a_c)) != 0;
		}

		std::string_view Trim(std::string_view a_s) noexcept
		{
			while (!a_s.empty() && IsSpace(a_s.front())) {
				a_s.remove_prefix(1);
			}
			while (!a_s.empty() && IsSpace(a_s.back())) {
				a_s.remove_suffix(1);
			}
			return a_s;
		}

		// Removes and returns the first whitespace-delimited token.
		std::string_view NextToken(std::string_view& a_s) noexcept
		{
			a_s = Trim(a_s);
			std::size_t end = 0;
			while (end < a_s.size() && !IsSpace(a_s[end])) {
				++end;
			}
			const auto token = a_s.substr(0, end);
			a_s.remove_prefix(end);
			a_s = Trim(a_s);
			return token;
		}

		bool IEquals(std::string_view a_a, std::string_view a_b) noexcept
		{
			return a_a.size() == a_b.size() &&
			       std::equal(a_a.begin(), a_a.end(), a_b.begin(), [](char a, char b) {
				       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			       });
		}

		// True for "f4mp", "F4MP" and "player.f4mp".
		bool IsCommandToken(std::string_view a_token) noexcept
		{
			if (const auto dot = a_token.rfind('.'); dot != std::string_view::npos) {
				a_token.remove_prefix(dot + 1);
			}
			return IEquals(a_token, COMMAND_SHORT);
		}

		std::string_view StripQuotes(std::string_view a_s) noexcept
		{
			if (a_s.size() >= 2 && a_s.front() == '"' && a_s.back() == '"') {
				a_s.remove_prefix(1);
				a_s.remove_suffix(1);
			}
			return Trim(a_s);
		}

		void Usage()
		{
			game::ConsolePrint("F4MP v" F4MP_VERSION_STRING " commands:");
			game::ConsolePrint("  f4mp status                  connection state");
			game::ConsolePrint("  f4mp connect [host[:port]]   connect, optionally to another server");
			game::ConsolePrint("  f4mp disconnect              leave the server (no auto-reconnect)");
			game::ConsolePrint("  f4mp say <text>              chat");
			game::ConsolePrint("  f4mp list                    players on the server");
			game::ConsolePrint("  f4mp name <name>             change your name");
			game::ConsolePrint("  f4mp unstick                 restore controls, HUD and camera after a menu froze you");
			game::ConsolePrint("  f4mp debug                   engine facts about every clone (3D, alpha, base record)");
			game::ConsolePrint("  f4mp clonebase <hex id>      NPC record to copy clones from (default 7), respawns them");
		}

		bool ExecuteCommand(
			const RE::SCRIPT_PARAMETER*,
			const char*,
			RE::TESObjectREFR*,
			RE::TESObjectREFR*,
			RE::Script* a_script,
			RE::ScriptLocals*,
			REX::Float32&,
			std::uint32_t&)
		{
			const char* text = a_script ? a_script->GetText() : nullptr;
			Execute(text ? std::string_view{ text } : std::string_view{});
			return true;
		}
	}

	bool Register()
	{
		auto* function = RE::SCRIPT_FUNCTION::GetConsoleFunctionByName(HIJACKED_COMMAND);
		if (!function) {
			REX::LogError("Console command \"{}\" not found; the f4mp console command is unavailable"sv, HIJACKED_COMMAND);
			return false;
		}

		function->functionName = COMMAND_NAME;
		function->shortName = COMMAND_SHORT;
		function->helpString = COMMAND_HELP;
		function->referenceFunction = false;
		function->paramCount = static_cast<std::uint16_t>(std::size(PARAMETERS));
		function->parameters = PARAMETERS;
		function->executeFunction = &ExecuteCommand;
		function->conditionFunction = nullptr;
		function->editorFilter = false;
		function->invalidatesCellList = false;

		REX::LogInformation("Console command \"f4mp\" installed in place of \"{}\""sv, HIJACKED_COMMAND);
		return true;
	}

	void Execute(std::string_view a_line)
	{
		auto rest = Trim(a_line);
		{
			auto probe = rest;
			if (IsCommandToken(NextToken(probe))) {
				rest = probe;
			}
		}
		const auto cmd = NextToken(rest);
		const auto args = rest;

		auto& session = Session::Get();

		if (cmd.empty() || IEquals(cmd, "help")) {
			Usage();
		} else if (IEquals(cmd, "status")) {
			session.PrintStatus();
		} else if (IEquals(cmd, "connect")) {
			if (!args.empty() && !session.SetServer(StripQuotes(args))) {
				game::ConsolePrint("[F4MP] Usage: f4mp connect [host[:port]]");
				return;
			}
			session.Connect(true);
		} else if (IEquals(cmd, "disconnect")) {
			session.Disconnect(true);
		} else if (IEquals(cmd, "say")) {
			const auto text = StripQuotes(args);
			if (text.empty()) {
				game::ConsolePrint("[F4MP] Usage: f4mp say <text>");
				return;
			}
			session.SendChat(std::string(text));
		} else if (IEquals(cmd, "list") || IEquals(cmd, "players")) {
			session.PrintPlayers();
		} else if (IEquals(cmd, "name")) {
			const auto name = StripQuotes(args);
			if (name.empty()) {
				game::ConsolePrint("[F4MP] Usage: f4mp name <name>");
				return;
			}
			session.SetPlayerName(name);
		} else if (IEquals(cmd, "unstick")) {
			game::RestorePlayerControl();
			game::ConsolePrint("[F4MP] Controls, HUD and camera restored.");
		} else if (IEquals(cmd, "debug")) {
			session.PrintDebug();
		} else if (IEquals(cmd, "clonebase")) {
			auto text = StripQuotes(args);
			if (text.size() > 2 && (text.substr(0, 2) == "0x" || text.substr(0, 2) == "0X")) {
				text.remove_prefix(2);
			}
			unsigned long id = 0;
			const auto res = std::from_chars(text.data(), text.data() + text.size(), id, 16);
			if (text.empty() || res.ec != std::errc{} || res.ptr != text.data() + text.size()) {
				game::ConsolePrint("[F4MP] Usage: f4mp clonebase <NPC form id in hex, e.g. 7 or A7D34>");
				return;
			}
			session.SetCloneBase(static_cast<RE::TESFormID>(id));
		} else {
			game::ConsolePrint("[F4MP] Unknown command \"" + std::string(cmd) + "\"");
			Usage();
		}
	}
}
