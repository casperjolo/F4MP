#include "PCH.hpp"

#include "Game/PrologueSkip.hpp"

#include "Game/GameUtil.hpp"

#include <atomic>
#include <cctype>
#include <charconv>

namespace f4mp::client
{
	namespace
	{
		// Fallout4.esm is always load slot 00, so these are absolute.
		constexpr RE::TESFormID MQ101_ID = 0x0001ED86; // "War Never Changes": the prologue
		constexpr RE::TESFormID MQ102_ID = 0x0001CC2A; // "Out of Time": starts when the player leaves the pod
		constexpr std::uint16_t MQ101_STARTED = 10;    // "Create character": the bathroom scene is up
		constexpr std::uint16_t MQ101_POD_OPENS = 900; // the stage that puts the player outside the pod

		constexpr auto LOOKS_MENU = "LooksMenu";     // face character creator
		constexpr auto SPECIAL_MENU = "SPECIALMenu"; // name + SPECIAL registration form

		constexpr float MENU_GRACE = 3.0f;            // seconds for a requested menu to actually appear
		constexpr float QUEST_START_FALLBACK = 45.0f; // run anyway if MQ101 never reports stage 10
		constexpr float SEX_CHANGE_SETTLE = 3.0f;     // seconds for the 3D to rebuild after a sex change

		// Set by the message box callback; -1 while unanswered.
		std::atomic<int> g_sexChoice{ -1 };

		class SexChoiceCallback final
			: public RE::IMessageBoxCallback
		{
		public:
			void operator()(std::uint8_t a_buttonIndex) override
			{
				g_sexChoice = a_buttonIndex;
			}
		};

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
		_armedAt = Clock::now();
		_questStartedAt = {};
		_next = 0;
		_faceMenuSeen = false;
		_specialMenuSeen = false;
		_loggedWaiting = false;
		_sexChanged = false;
		g_sexChoice = -1;
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
			// Console commands are ignored until the game has really started; MQ101 reaching
			// stage 10 (the bathroom scene) is the reliable sign of that.
			if (!game::GetPlayer()) {
				return;
			}
			const auto stage = StageOf(MQ101_ID);
			if (stage < MQ101_STARTED) {
				if (!_loggedWaiting) {
					_loggedWaiting = true;
					REX::LogInformation("Prologue skip: player placed, waiting for MQ101 to start (stage {})"sv, stage);
				}
				if (now - _armedAt < Seconds(QUEST_START_FALLBACK)) {
					return;
				}
				REX::LogWarning("Prologue skip: MQ101 still at stage {} after {} s, running anyway"sv, stage, QUEST_START_FALLBACK);
			}
			if (_questStartedAt == Clock::time_point{}) {
				_questStartedAt = now;
				REX::LogInformation("Prologue skip: MQ101 at stage {}, starting in {} s"sv, stage, _settings.startDelay);
			}
			if (now - _questStartedAt < Seconds(_settings.startDelay)) {
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
				if (!_settings.chargen) {
					Finish("done");
				} else if (_settings.chargenMode == 0) {
					AskSex();
				} else {
					OpenFaceMenu();
				}
				return;
			}
			if (now - _stateSince > Seconds(_settings.podTimeout)) {
				REX::LogWarning("Prologue skip: gave up waiting for the pod (MQ101 stage {}, MQ102 stage {})"sv, mq101, mq102);
				Finish("the pod did not open in time; the vault exit still offers character customisation");
			}
			return;
		}

		case State::kSexChoice: {
			if (now < _waitUntil) {
				return; // letting a sex change settle before the face editor
			}
			if (_sexChanged) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (player && !player->Is3DLoaded()) {
					return; // the swap rebuilds the whole character; the face editor needs it finished
				}
			}
			const int choice = g_sexChoice.load();
			if (choice < 0) {
				if (now - _stateSince > Seconds(_settings.podTimeout)) {
					REX::LogWarning("Prologue skip: no answer to the sex prompt, going on with the face editor"sv);
					OpenFaceMenu();
				}
				return;
			}
			ApplySexChoice(choice);
			return;
		}

		case State::kFaceMenu: {
			if (IsMenuOpen(LOOKS_MENU)) {
				_faceMenuSeen = true;
				return;
			}
			if (!_faceMenuSeen && now - _stateSince < Seconds(MENU_GRACE)) {
				return; // give it a moment to appear
			}
			if (!_faceMenuSeen) {
				REX::LogWarning("Prologue skip: the race menu never opened"sv);
			}
			OpenSpecialMenu();
			_specialMenuSeen = false;
			_stateSince = now;
			_state = State::kSpecialMenu;
			return;
		}

		case State::kSpecialMenu: {
			if (IsMenuOpen(SPECIAL_MENU)) {
				_specialMenuSeen = true;
				return;
			}
			if (!_specialMenuSeen && now - _stateSince < Seconds(MENU_GRACE)) {
				return;
			}
			if (!_specialMenuSeen) {
				REX::LogWarning("Prologue skip: the SPECIAL menu never opened"sv);
			}
			// The vanilla flow re-enables controls, HUD and camera from its quest script once the
			// menus are done; nothing does that for us, so do it here.
			game::RestorePlayerControl();
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

		// The bathroom mirror opens the character creator at stage 10; close it, it comes back
		// once the player is out of the pod.
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

	void PrologueSkip::AskSex()
	{
		auto* manager = RE::MessageMenuManager::GetSingleton();
		if (!manager) {
			REX::LogWarning("Prologue skip: no message box manager, skipping the sex prompt"sv);
			OpenFaceMenu();
			return;
		}

		g_sexChoice = -1;
		// The engine keeps its own reference to the callback and releases it when done.
		manager->CreateMessage("F4MP\n\nWho are you?", new SexChoiceCallback(), "Male", "Female");
		REX::LogInformation("Prologue skip: asking for the sex"sv);

		_waitUntil = Clock::now();
		_stateSince = Clock::now();
		_state = State::kSexChoice;
	}

	void PrologueSkip::ApplySexChoice(int a_choice)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto wanted = a_choice == 1 ? RE::SEX::kFemale : RE::SEX::kMale;
		const auto current = player ? player->GetSex() : RE::SEX::kNone;
		REX::LogInformation("Prologue skip: sex choice {} (currently {})"sv, wanted, current);

		if (current != wanted && !_sexChanged) {
			// The stock console command flips the base actor's sex and rebuilds the 3D. Stay in
			// kSexChoice (the answer is kept) so this runs again once the swap has settled, and
			// then falls through to the face editor whatever the result.
			_sexChanged = true;
			RE::Script::ExecuteSingleLineConsoleCommand("player.sexchange", nullptr, false);
			REX::LogInformation("Prologue skip: sex changed, waiting for the character to rebuild"sv);
			_waitUntil = Clock::now() + Seconds(SEX_CHANGE_SETTLE);
			return;
		}
		if (current != wanted) {
			REX::LogWarning("Prologue skip: the sex change did not take; the vault exit offers another chance"sv);
		}

		OpenFaceMenu();
	}

	void PrologueSkip::OpenFaceMenu()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto vm = RE::GameVM::GetVMInterface();
		if (!player || !vm) {
			Finish("no player or script VM for the character creator");
			return;
		}

		// Mode 1 ("remake") edits the player alone; mode 0 needs the two pre-war spouse actors
		// side by side, which are unloaded by the time we are in 2287, so the sex is handled by
		// the prompt instead.
		constexpr std::int32_t mode = 1;
		REX::LogInformation("Prologue skip: Game.ShowRaceMenu(player, {})"sv, mode);

		const auto ok = vm->InvokeStaticFunction(
			RE::BSFixedString("Game"),
			RE::BSFixedString("ShowRaceMenu"),
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{},
			static_cast<RE::TESObjectREFR*>(player),
			mode,
			static_cast<RE::TESObjectREFR*>(nullptr),
			static_cast<RE::TESObjectREFR*>(nullptr),
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
}
