#include "PCH.hpp"

#include "Game/PrologueSkip.hpp"

#include "Game/GameUtil.hpp"

#include <cctype>
#include <charconv>

namespace f4mp::client
{
	namespace
	{
		// Fallout4.esm is always load slot 00, so these are absolute.
		constexpr RE::TESFormID MQ101_ID = 0x0001ED86; // "War Never Changes": the prologue
		constexpr RE::TESFormID MQ102_ID = 0x0001CC2A; // "Out of Time": starts when the player leaves the pod
		constexpr std::uint16_t MQ101_POD_OPENS = 900; // the stage that puts the player outside the pod

		constexpr auto LOOKS_MENU = "LooksMenu";     // face / sex character creator
		constexpr auto SPECIAL_MENU = "SPECIALMenu"; // name + SPECIAL registration form

		constexpr float FACE_MENU_GRACE = 3.0f; // seconds for the race menu to actually appear

		std::chrono::steady_clock::duration Seconds(float a_seconds)
		{
			return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(a_seconds));
		}

		bool StartsWithIgnoreCase(std::string_view a_s, std::string_view a_prefix) noexcept
		{
			if (a_s.size() < a_prefix.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_prefix.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a_s[i])) != std::tolower(static_cast<unsigned char>(a_prefix[i]))) {
					return false;
				}
			}
			return true;
		}

		bool ContainsIgnoreCase(std::string_view a_s, std::string_view a_needle) noexcept
		{
			if (a_needle.empty() || a_s.size() < a_needle.size()) {
				return false;
			}
			for (std::size_t i = 0; i + a_needle.size() <= a_s.size(); ++i) {
				if (StartsWithIgnoreCase(a_s.substr(i), a_needle)) {
					return true;
				}
			}
			return false;
		}
	}

	void PrologueSkip::Configure(Settings a_settings)
	{
		_settings = std::move(a_settings);
	}

	void PrologueSkip::OnNewGame()
	{
		if (!_settings.enabled) {
			_state = State::kDone;
			return;
		}
		_state = State::kArmed;
		_placedAt = {};
		_next = 0;
		_faceMenuSeen = false;
		REX::LogInformation("Prologue skip: armed"sv);
	}

	void PrologueSkip::OnLeaveWorld()
	{
		if (IsActive()) {
			REX::LogInformation("Prologue skip: cancelled (leaving the world)"sv);
		}
		_state = State::kIdle;
	}

	// ---- per frame ------------------------------------------------------------------

	void PrologueSkip::Update()
	{
		const auto now = Clock::now();

		switch (_state) {
		case State::kIdle:
		case State::kDone:
			return;

		case State::kArmed: {
			if (!game::GetPlayer()) {
				_placedAt = {};
				return;
			}
			if (_placedAt == Clock::time_point{}) {
				_placedAt = now;
				return;
			}
			if (now - _placedAt < Seconds(_settings.startDelay)) {
				return;
			}
			Start();
			return;
		}

		case State::kRunning: {
			if (now < _waitUntil) {
				return;
			}
			// Run consecutive commands in one frame, exactly like typing them; stop at a wait.
			while (_state == State::kRunning && RunNextCommand()) {
			}
			return;
		}

		case State::kWaitForPod: {
			const auto mq101 = StageOf(MQ101_ID);
			const auto mq102 = StageOf(MQ102_ID);
			if (mq102 >= 1 || mq101 >= 1000) {
				REX::LogInformation("Prologue skip: out of the pod (MQ101 stage {}, MQ102 stage {})"sv, mq101, mq102);
				if (_settings.chargen) {
					OpenFaceMenu();
				} else {
					Finish("done");
				}
				return;
			}
			if (now - _stateSince > Seconds(_settings.podTimeout)) {
				REX::LogWarning("Prologue skip: gave up waiting for the pod (MQ101 stage {}, MQ102 stage {})"sv, mq101, mq102);
				Finish("the pod did not open in time; the vault exit still offers character customisation");
			}
			return;
		}

		case State::kFaceMenu: {
			if (IsMenuOpen(LOOKS_MENU)) {
				_faceMenuSeen = true;
				return;
			}
			if (!_faceMenuSeen && now - _stateSince < Seconds(FACE_MENU_GRACE)) {
				return; // give it a moment to appear
			}
			if (!_faceMenuSeen) {
				REX::LogWarning("Prologue skip: the race menu never opened"sv);
			}
			OpenSpecialMenu();
			Finish("character creation done");
			return;
		}
		}
	}

	// ---- steps ----------------------------------------------------------------------

	void PrologueSkip::Start()
	{
		const auto mq101 = StageOf(MQ101_ID);
		const auto mq102 = StageOf(MQ102_ID);
		if (mq101 >= MQ101_POD_OPENS || mq102 >= 1) {
			Finish("prologue already past the pod, nothing to skip");
			return;
		}

		REX::LogInformation("Prologue skip: starting at MQ101 stage {} with {} command(s)"sv, mq101, _settings.commands.size());
		game::ConsolePrint("[F4MP] Skipping the prologue...");

		// The bathroom mirror may already have opened the character creator; close it, it comes
		// back once the player is out of the pod.
		if (IsMenuOpen(LOOKS_MENU)) {
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::BSFixedString(LOOKS_MENU), RE::UI_MESSAGE_TYPE::kHide);
				REX::LogInformation("Prologue skip: closed the mirror character creator"sv);
			}
		}

		_next = 0;
		_waitUntil = Clock::now();
		_stateSince = Clock::now();
		_state = State::kRunning;
	}

	bool PrologueSkip::RunNextCommand()
	{
		if (_next >= _settings.commands.size()) {
			_stateSince = Clock::now();
			if (_settings.chargen) {
				_state = State::kWaitForPod;
				REX::LogInformation("Prologue skip: commands done, waiting for the pod to open"sv);
			} else {
				Finish("commands done");
			}
			return false;
		}

		const auto& cmd = _settings.commands[_next++];

		if (StartsWithIgnoreCase(cmd, "wait ")) {
			float seconds = 1.0f;
			const auto text = std::string_view(cmd).substr(5);
			std::from_chars(text.data(), text.data() + text.size(), seconds);
			_waitUntil = Clock::now() + Seconds(seconds);
			REX::LogInformation("Prologue skip: waiting {} s"sv, seconds);
			return false;
		}

		REX::LogInformation("Prologue skip: > {}"sv, cmd);
		if (!RE::Script::ExecuteSingleLineConsoleCommand(cmd, nullptr, false)) {
			REX::LogWarning("Prologue skip: command did not run: {}"sv, cmd);
		}
		return true;
	}

	void PrologueSkip::OpenFaceMenu()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto vm = RE::GameVM::GetVMInterface();
		if (!player || !vm) {
			Finish("no player or script VM for the character creator");
			return;
		}

		auto mode = _settings.chargenMode;
		RE::TESObjectREFR* spouseFemale = nullptr;
		RE::TESObjectREFR* spouseMale = nullptr;
		if (mode == 0) {
			// The full creator swaps between the two spouse actors when the sex is changed.
			auto* mq101 = RE::TESForm::FindFormByID<RE::TESQuest>(MQ101_ID);
			spouseFemale = AliasRef(mq101, "SpouseFemale");
			spouseMale = AliasRef(mq101, "SpouseMale");
			if (!spouseFemale || !spouseMale) {
				REX::LogWarning("Prologue skip: spouse aliases not found on MQ101; using remake mode (no sex change)"sv);
				mode = 1;
			}
		}

		REX::LogInformation("Prologue skip: Game.ShowRaceMenu(player, {}, 0x{:08X}, 0x{:08X})"sv,
			mode, spouseFemale ? spouseFemale->GetFormID() : 0u, spouseMale ? spouseMale->GetFormID() : 0u);

		const auto ok = vm->InvokeStaticFunction(
			RE::BSFixedString("Game"),
			RE::BSFixedString("ShowRaceMenu"),
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{},
			static_cast<RE::TESObjectREFR*>(player),
			mode,
			spouseFemale,
			spouseMale,
			static_cast<RE::TESObjectREFR*>(nullptr));
		if (!ok) {
			REX::LogWarning("Prologue skip: Game.ShowRaceMenu could not be dispatched"sv);
		}

		_faceMenuSeen = false;
		_stateSince = Clock::now();
		_state = State::kFaceMenu;
	}

	void PrologueSkip::OpenSpecialMenu()
	{
		if (IsMenuOpen(SPECIAL_MENU)) {
			REX::LogInformation("Prologue skip: SPECIAL menu is already open"sv);
			return;
		}
		auto vm = RE::GameVM::GetVMInterface();
		if (!vm) {
			return;
		}
		REX::LogInformation("Prologue skip: Game.ShowSPECIALMenu()"sv);
		const auto ok = vm->InvokeStaticFunction(
			RE::BSFixedString("Game"),
			RE::BSFixedString("ShowSPECIALMenu"),
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{});
		if (!ok) {
			REX::LogWarning("Prologue skip: Game.ShowSPECIALMenu could not be dispatched"sv);
		}
	}

	void PrologueSkip::Finish(std::string_view a_why)
	{
		REX::LogInformation("Prologue skip: finished ({})"sv, a_why);
		_state = State::kDone;
	}

	// ---- helpers --------------------------------------------------------------------

	std::uint16_t PrologueSkip::StageOf(RE::TESFormID a_quest)
	{
		auto* quest = RE::TESForm::FindFormByID<RE::TESQuest>(a_quest);
		return quest ? quest->currentStage : 0;
	}

	bool PrologueSkip::IsMenuOpen(const char* a_menu)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		return ui->IsMenuOpen(RE::BSFixedString(a_menu)).value_or(false);
	}

	RE::TESObjectREFR* PrologueSkip::AliasRef(RE::TESQuest* a_quest, std::string_view a_nameContains)
	{
		if (!a_quest) {
			return nullptr;
		}

		for (auto* alias : a_quest->aliases) {
			if (!alias) {
				continue;
			}
			const auto name = std::string_view(alias->aliasName.c_str());
			if (!ContainsIgnoreCase(name, a_nameContains)) {
				continue;
			}
			RE::ObjectRefHandle handle;
			a_quest->GetAliasedRef(&handle, alias->aliasID);
			if (auto ref = handle.get()) {
				REX::LogInformation("Prologue skip: alias \"{}\" -> 0x{:08X}"sv, name, ref->GetFormID());
				return ref.get();
			}
		}

		// Help whoever reads the log figure out the real alias names.
		std::string names;
		for (auto* alias : a_quest->aliases) {
			if (alias) {
				names += names.empty() ? "" : ", ";
				names += alias->aliasName.c_str();
			}
		}
		REX::LogWarning("Prologue skip: no filled alias containing \"{}\"; quest aliases: {}"sv, a_nameContains, names);
		return nullptr;
	}
}
