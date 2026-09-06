#pragma once

namespace f4mp::client
{
	// Skips the pre-war Sanctuary Hills prologue of a new game. The player wakes up in their
	// Vault 111 cryo pod and gets the character creator (face, then name + SPECIAL) right there,
	// so everyone starts the same way and nobody runs to the vault.
	//
	// The skip itself is a list of console commands (configurable in F4MP.ini) built on the
	// sequence the community uses for the same purpose. The character creator is opened through
	// the Papyrus functions the game itself uses: Game.ShowRaceMenu and Game.ShowSPECIALMenu.
	class PrologueSkip
	{
	public:
		struct Settings
		{
			bool enabled{ true };
			float startDelay{ 2.0f };   // seconds after the player is placed before the skip starts
			float podTimeout{ 120.0f }; // seconds to wait for the pod to open before giving up on chargen
			std::vector<std::string> commands; // console commands in order; "wait <seconds>" pauses
			bool chargen{ true };
			std::int32_t chargenMode{ 0 }; // Game.ShowRaceMenu uiMode: 0 full (sex + face), 1 remake (face only)
		};

		void Configure(Settings a_settings);

		void OnNewGame();    // arms the skip
		void OnLeaveWorld(); // cancels it (loading a save, quitting to the menu)

		// Once per frame on the game thread while in the world.
		void Update();

		[[nodiscard]] bool IsActive() const noexcept { return _state != State::kIdle && _state != State::kDone; }

	private:
		enum class State
		{
			kIdle,
			kArmed,      // new game started, waiting for the player to be placed in a cell
			kRunning,    // executing the command list
			kWaitForPod, // commands done, waiting for the player to leave the cryo pod
			kFaceMenu,   // race menu requested, waiting for it to close
			kDone
		};

		using Clock = std::chrono::steady_clock;

		void Start();
		// Returns false when it stopped for a wait or because the list is exhausted.
		bool RunNextCommand();
		void OpenFaceMenu();
		void OpenSpecialMenu();
		void Finish(std::string_view a_why);

		[[nodiscard]] static std::uint16_t StageOf(RE::TESFormID a_quest);
		[[nodiscard]] static bool IsMenuOpen(const char* a_menu);
		[[nodiscard]] static RE::TESObjectREFR* AliasRef(RE::TESQuest* a_quest, std::string_view a_nameContains);

		Settings _settings;
		State _state{ State::kIdle };
		std::size_t _next{ 0 };
		Clock::time_point _placedAt{};
		Clock::time_point _stateSince{};
		Clock::time_point _waitUntil{};
		bool _faceMenuSeen{ false };
	};
}
