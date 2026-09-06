#include "PCH.hpp"

#include "Config.hpp"
#include "Game/ConsoleCommands.hpp"
#include "Session.hpp"

namespace
{
	void __cdecl OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		using MessageType = F4SE::MessagingInterface::MessageType;

		auto& session = f4mp::client::Session::Get();
		switch (a_msg->GetType()) {
		case MessageType::kGameDataReady:
			session.OnGameDataReady();
			break;
		case MessageType::kPostLoadGame:
		case MessageType::kNewGame:
			session.OnEnterWorld();
			break;
		case MessageType::kPreLoadGame:
			session.OnLeaveWorld();
			break;
		case MessageType::kPreSaveGame:
			session.OnPreSave();
			break;
		default:
			break;
		}
	}

	void Tick()
	{
		f4mp::client::Session::Get().Tick();
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, F4SE::InitInfo{ .logName = "F4MP" });

	REX::LogInformation("F4MP v{} loading (F4SE {}, runtime {})"sv,
		F4MP_VERSION_STRING, F4SE::GetF4SEVersion(), F4SE::GetRuntimeVersion());

	f4mp::client::Session::Get().Init(f4mp::client::Config::Load());

	// "f4mp ..." in the ~ console: status, connect, say, ...
	f4mp::client::console::Register();

	F4SE::GetMessagingInterface()->RegisterListener(&OnF4SEMessage);

	// Runs once per frame on the game thread: network pump, state send, remote actor updates.
	F4SE::GetTaskInterface()->AddTaskPermanent(std::function<void()>{ &Tick });

	REX::LogInformation("F4MP loaded"sv);
	return true;
}
