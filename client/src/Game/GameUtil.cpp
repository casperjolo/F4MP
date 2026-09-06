#include "PCH.hpp"

#include "Game/GameUtil.hpp"

namespace f4mp::client::game
{
	namespace
	{
		// NEW_REFR_DATA is declared novtable; give the instance the engine's own vtable so the
		// engine's HandlePre3D (whatever setup it does for the new reference) runs, exactly as it
		// does for a Papyrus PlaceAtMe. An earlier no-op override left actors in a T-pose.
		void EmplaceEngineVTable(RE::NEW_REFR_DATA& a_data)
		{
			static const REL::Relocation<std::uintptr_t> VTBL{ RE::VTABLE::NEW_REFR_DATA[0] };
			*reinterpret_cast<std::uintptr_t*>(std::addressof(a_data)) = VTBL.get();
		}
	}

	RE::PlayerCharacter* GetPlayer()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return nullptr;
		}
		return player;
	}

	std::optional<Space> GetSpace(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return std::nullopt;
		}

		auto* cell = a_ref->GetParentCell();
		if (!cell) {
			return std::nullopt;
		}

		Space space;
		space.cellId = cell->GetFormID();
		space.interior = cell->IsInterior();
		if (auto* world = cell->GetWorldSpace()) {
			space.worldspaceId = world->GetFormID();
		}
		return space;
	}

	Space SpaceFromState(const PlayerState& a_state)
	{
		Space space;
		space.worldspaceId = a_state.worldspaceId;
		space.cellId = a_state.cellId;
		space.interior = a_state.worldspaceId == 0;
		return space;
	}

	std::string GetPlayerName()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* base = player ? player->GetActorBase() : nullptr;
		if (base) {
			const char* name = base->GetFullName();
			if (name && *name) {
				return name;
			}
		}
		return "Wastelander";
	}

	void ConsolePrint(std::string_view a_text)
	{
		auto* log = RE::ConsoleLog::GetSingleton();
		if (!log) {
			return;
		}
		std::string line(a_text);
		line.push_back('\n');
		log->AddString(line.c_str());
	}

	RE::NiPoint3 ToNi(const Vec3& a_v)
	{
		return RE::NiPoint3(a_v.x, a_v.y, a_v.z);
	}

	// ---- clone base form -------------------------------------------------------------

	RE::TESNPC* CreateCloneBase(RE::TESFormID a_source, const std::string& a_name, bool a_ghost)
	{
		auto* source = RE::TESForm::FindFormByID<RE::TESNPC>(a_source);
		if (!source) {
			REX::LogError("CreateCloneBase: NPC form 0x{:08X} missing"sv, a_source);
			return nullptr;
		}

		auto* form = source->CreateDuplicateForm(false, nullptr);
		auto* npc = form ? form->As<RE::TESNPC>() : nullptr;
		if (!npc) {
			REX::LogError("CreateCloneBase: CreateDuplicateForm failed"sv);
			return nullptr;
		}

		using Flags = RE::ACTOR_BASE_DATA::Flags;
		auto& flags = npc->actorData.actorBaseFlags;
		flags.set(a_ghost, Flags::kIsGhost);
		flags.reset(Flags::kUnique, Flags::kEssential, Flags::kProtected, Flags::kIsChargenFacePreset);

		npc->aiData.aggression = 0;     // unaggressive
		npc->aiData.assistance = 0;     // helps nobody
		npc->aiData.useAggroRadius = 0;

		RenameCloneBase(npc, a_name);
		REX::LogInformation("Created clone base 0x{:08X} \"{}\" from 0x{:08X} (heads {}, skin 0x{:08X}, sex {})"sv,
			npc->GetFormID(), a_name, a_source, npc->GetHeadParts().size(),
			npc->formSkin ? npc->formSkin->GetFormID() : 0u, npc->GetSex());
		return npc;
	}

	void RenameCloneBase(RE::TESNPC* a_base, const std::string& a_name)
	{
		if (!a_base) {
			return;
		}
		RE::TESFullName::SetFormFullName(a_base, RE::BGSLocalizedString(a_name.c_str()), false);
	}

	// ---- clone actor -------------------------------------------------------------------

	RE::ObjectRefHandle SpawnPlayerClone(RE::TESNPC* a_base, const RE::NiPoint3& a_position, float a_yaw, const Space& a_space)
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!a_base || !handler) {
			REX::LogError("SpawnPlayerClone: base form or data handler unavailable"sv);
			return {};
		}

		RE::TESObjectCELL* interior = nullptr;
		RE::TESWorldSpace* world = nullptr;
		if (a_space.interior) {
			interior = RE::TESForm::FindFormByID<RE::TESObjectCELL>(a_space.cellId);
		} else {
			world = RE::TESForm::FindFormByID<RE::TESWorldSpace>(a_space.worldspaceId);
		}
		if (!interior && !world) {
			REX::LogWarning("SpawnPlayerClone: unknown space (ws=0x{:08X} cell=0x{:08X})"sv, a_space.worldspaceId, a_space.cellId);
			return {};
		}

		RE::NEW_REFR_DATA data;
		EmplaceEngineVTable(data);
		data.location = a_position;
		data.direction = RE::NiPoint3(0.0f, 0.0f, a_yaw);
		data.object = a_base;
		data.interior = interior;
		data.world = world;
		data.forcePersist = false;
		data.initializeScripts = true;

		return handler->CreateReferenceAtLocation(data);
	}

	bool PacifyClone(RE::Actor* a_actor)
	{
		if (!a_actor || !a_actor->currentProcess) {
			return false;
		}
		a_actor->InitiateDoNothingPackage();
		if (a_actor->IsInCombat()) {
			a_actor->StopCombat();
		}
		return true;
	}

	void ApplyStateFlags(RE::Actor* a_actor, std::uint8_t a_flags, std::uint8_t a_previous)
	{
		if (!a_actor) {
			return;
		}

		const std::uint8_t changed = a_flags ^ a_previous;
		if (changed & kStateSneaking) {
			a_actor->SetSneaking((a_flags & kStateSneaking) != 0);
		}
		if (changed & kStateWeaponDrawn) {
			a_actor->DrawWeaponMagicHands((a_flags & kStateWeaponDrawn) != 0);
		}
		if (changed & kStateSprinting) {
			a_actor->sprinting = (a_flags & kStateSprinting) != 0 ? 1u : 0u;
		}
	}

	void Despawn(RE::ObjectRefHandle& a_handle)
	{
		if (auto ref = a_handle.get()) {
			ref->Disable();
			ref->SetDelete(true);
		}
		a_handle.reset();
	}

	void RestorePlayerControl()
	{
		// 1. Input enable layers. Quest scripts disable movement etc. through persistent layers
		//    (InputEnableLayer in Papyrus) that survive saves and that the console command
		//    EnablePlayerControls does not touch. Re-enable every event on every layer.
		std::size_t layers = 0;
		if (auto* input = RE::BSInputEnableManager::GetSingleton()) {
			for (const auto& layer : input->layerWrappers) {
				if (!layer) {
					continue;
				}
				const auto id = layer->GetLayerID();
				input->EnableUserEvent(id, RE::UserEvents::USER_EVENT_FLAG::kAll, true, RE::UserEvents::SENDER_ID::kScript);
				input->EnableOtherEvent(id, RE::OtherInputEvents::OTHER_EVENT_FLAG::kAll, true, RE::UserEvents::SENDER_ID::kScript);
				++layers;
			}
		}

		// 2. Legacy control flags and the prologue's "shivering walk" animation archetype.
		RE::Script::ExecuteSingleLineConsoleCommand("EnablePlayerControls", nullptr, true);
		RE::Script::ExecuteSingleLineConsoleCommand("player.ChangeAnimArchetype", nullptr, true);

		// 3. Chargen state: saving/waiting locks and the HUD mode that hides the HUD.
		if (auto vm = RE::GameVM::GetVMInterface()) {
			using Callback = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>;
			vm->InvokeStaticFunction(RE::BSFixedString("Game"), RE::BSFixedString("SetInCharGen"), Callback{}, false, false, false);
			vm->InvokeStaticFunction(RE::BSFixedString("Game"), RE::BSFixedString("SetCharGenHUDMode"), Callback{}, static_cast<std::int32_t>(0));
			vm->InvokeStaticFunction(RE::BSFixedString("Game"), RE::BSFixedString("ForceFirstPerson"), Callback{});
		}

		// 4. The HUD menu itself, in case it was closed rather than just hidden.
		auto* ui = RE::UI::GetSingleton();
		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (ui && queue && !ui->IsMenuOpen(RE::BSFixedString("HUDMenu")).value_or(true)) {
			queue->AddMessage(RE::BSFixedString("HUDMenu"), RE::UI_MESSAGE_TYPE::kShow);
		}

		REX::LogInformation("Restored player control: {} input layer(s) re-enabled, chargen flags cleared, HUD mode reset, first person"sv, layers);
	}
}
