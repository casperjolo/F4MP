#include "PCH.hpp"

#include "Game/GameUtil.hpp"

#include <format>

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
		// NOTE: NEW_REFR_DATA::reference is *not* an anchor. When it is set the engine
		// initialises that existing reference instead of creating one; setting it to the
		// player made the "clone" the local player, who then got flown around in orbit.
		data.reference = nullptr;
		// Mirror the Papyrus PlaceAtMe native exactly: of all the flag bytes it sets only
		// clearStillLoadingFlag (FO4_Wrld's decompilation: "NEW_REFR_DATA flags = 0x1000000
		// only"). A new reference stays marked "still loading" until its creator clears the
		// mark, and a still-loading actor never starts updating: no fade-in, no animation
		// graph, no movement. That was the T-pose.
		data.forcePersist = false;
		data.clearStillLoadingFlag = true;
		data.initializeScripts = false;
		data.initiallyDisabled = false;

		auto handle = handler->CreateReferenceAtLocation(data);
		if (auto ref = handle.get()) {
			if (ref.get() == RE::PlayerCharacter::GetSingleton()) {
				REX::LogError("SpawnPlayerClone: the engine handed back the local player; refusing to drive it"sv);
				return {};
			}
			REX::LogInformation("Placed 0x{:08X} from 0x{:08X} in cell 0x{:08X}"sv,
				ref->GetFormID(), a_base->GetFormID(), ref->GetParentCell() ? ref->GetParentCell()->GetFormID() : 0u);
		}
		return handle;
	}

	void ApplyCloneIdentity(RE::TESObjectREFR* a_ref, const std::string& a_name, bool a_ghost)
	{
		if (!a_ref) {
			return;
		}
		a_ref->SetDisplayName(RE::BGSLocalizedString(a_name.c_str()));
		if (a_ghost) {
			// Per-reference ghost through the stock console command, targeted at this actor.
			RE::Script::ExecuteSingleLineConsoleCommand("SetGhost 1", a_ref, true);
		}
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

	void DriveClone(RE::Actor* a_actor, const RE::NiPoint3& a_position, float a_yaw)
	{
		if (!a_actor) {
			return;
		}
		a_actor->SetPosition(a_position, true);
		a_actor->SetHeading(a_yaw);
		// The engine syncs the 3D from the reference position during the actor's own update;
		// depending on where in the frame this runs the mesh would otherwise trail by a frame.
		a_actor->Update3DPosition(true);
	}

	void DriveCloneByMove(RE::Actor* a_actor, const RE::NiPoint3& a_position, float a_yaw, float a_deltaTime)
	{
		if (!a_actor) {
			return;
		}
		constexpr float SNAP_DISTANCE = 250.0f;

		const auto current = a_actor->GetPosition();
		const RE::NiPoint3 delta(a_position.x - current.x, a_position.y - current.y, a_position.z - current.z);
		const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
		if (distance > SNAP_DISTANCE) {
			DriveClone(a_actor, a_position, a_yaw);
			return;
		}
		a_actor->SetHeading(a_yaw);
		a_actor->Move(a_deltaTime, delta, false);
	}

	float ReadGraphFloat(RE::Actor* a_actor, const char* a_variable)
	{
		float value = -1.0f;
		if (a_actor) {
			a_actor->GetGraphVariableImplFloat(RE::BSFixedString(a_variable), value);
		}
		return value;
	}

	bool ReadGraphBool(RE::Actor* a_actor, const char* a_variable)
	{
		bool value = false;
		if (a_actor) {
			a_actor->GetGraphVariableImplBool(RE::BSFixedString(a_variable), value);
		}
		return value;
	}

	std::string GraphValue::Describe() const
	{
		if (!Exists()) {
			return "-";
		}
		std::string out;
		if (asFloat) {
			out += std::format("f={:.2f}", f);
		}
		if (asInt) {
			out += (out.empty() ? "" : " ") + std::format("i={}", i);
		}
		if (asBool) {
			out += (out.empty() ? "" : " ") + std::format("b={}", b);
		}
		return out;
	}

	GraphValue ReadGraphValue(RE::Actor* a_actor, const char* a_variable)
	{
		GraphValue value;
		if (!a_actor) {
			return value;
		}
		const RE::BSFixedString name(a_variable);
		value.asFloat = a_actor->GetGraphVariableImplFloat(name, value.f);
		value.asInt = a_actor->GetGraphVariableImplInt(name, value.i);
		value.asBool = a_actor->GetGraphVariableImplBool(name, value.b);
		return value;
	}

	std::span<const char* const> GraphVariableCandidates()
	{
		// Names seen in Bethesda behaviour graphs plus the ones FO4_Wrld drives. Probing is a
		// hash lookup each, so a wide net costs nothing.
		static const char* const NAMES[] = {
			// locomotion
			"Speed", "SpeedSampled", "SpeedDamped", "fSpeed", "MoveSpeed", "fMoveSpeed",
			"Direction", "DirectionSampled", "DirectionDamped", "TurnDelta", "fTurnDelta",
			"LocomotionSpeed", "fLocomotionSpeed", "SpeedTarget", "DesiredSpeed",
			// movement state
			"IsRunning", "IsSprinting", "IsWalking", "IsMoving", "bIsMoving", "bIsSynced",
			"iState", "iSyncTurnState", "iSyncIdleLocomotion", "bMotionDriven", "bAnimationDriven",
			// posture / actions
			"iIsInSneak", "IsBlocking", "IsAttacking", "bAimActive", "IsDead", "bEquipOk",
			"bInJumpState", "fFallTime", "bIsStaggering", "IsCasting", "iLeftHandType",
			"iRightHandType", "bWantsToSprint", "bIsCrouching"
		};
		return { NAMES };
	}

	void SetLocomotion(RE::Actor* a_actor, float a_speed, bool a_sprinting, bool a_first)
	{
		if (!a_actor) {
			return;
		}

		// Which variable carries locomotion speed differs between behaviour graphs, and writing
		// to a name the graph does not have is a silent no-op ("SpeedSampled" reads back as
		// missing on a clone, while "Direction" reads back fine). So write to every candidate
		// the graph actually has, and say in the log which ones those were.
		static const char* const SPEED_NAMES[] = { "SpeedSampled", "Speed", "SpeedDamped", "fSpeed", "MoveSpeed" };
		static const char* const RUN_NAMES[] = { "IsRunning", "bIsRunning" };
		static const char* const SPRINT_NAMES[] = { "IsSprinting", "bIsSprinting" };

		constexpr float STOP_SPEED = 8.0f;  // below this the clone is standing (units/s)
		constexpr float RUN_SPEED = 260.0f; // walk/run split; walking is ~110-160, running ~350+
		constexpr float MAX_SPEED = 900.0f;

		float speed = std::clamp(a_speed, 0.0f, MAX_SPEED);
		if (speed < STOP_SPEED) {
			speed = 0.0f;
		}

		std::string wrote;
		const auto writeFloat = [&](const char* a_name, float a_value) {
			if (!ReadGraphValue(a_actor, a_name).asFloat) {
				return;
			}
			if (a_actor->SetGraphVariableFloat(RE::BSFixedString(a_name), a_value) && a_first) {
				wrote += (wrote.empty() ? "" : ", ") + std::string(a_name);
			}
		};
		const auto writeBool = [&](const char* a_name, bool a_value) {
			if (!ReadGraphValue(a_actor, a_name).asBool) {
				return;
			}
			if (a_actor->SetGraphVariableBool(RE::BSFixedString(a_name), a_value) && a_first) {
				wrote += (wrote.empty() ? "" : ", ") + std::string(a_name);
			}
		};

		if (a_first) {
			// Root motion off, so a clip plays without shoving the actor away from our position.
			writeBool("bAnimationDriven", false);
			writeBool("bMotionDriven", false);
		}

		for (const auto* name : SPEED_NAMES) {
			writeFloat(name, speed);
		}
		writeFloat("Direction", 0.0f); // moving the way it faces
		for (const auto* name : RUN_NAMES) {
			writeBool(name, speed >= RUN_SPEED);
		}
		for (const auto* name : SPRINT_NAMES) {
			writeBool(name, a_sprinting && speed > 0.0f);
		}

		if (a_first) {
			REX::LogInformation("Locomotion on 0x{:08X}: wrote [{}]"sv, a_actor->GetFormID(), wrote.empty() ? "nothing" : wrote);
		}
	}

	void Despawn(RE::ObjectRefHandle& a_handle)
	{
		if (auto ref = a_handle.get()) {
			if (ref.get() == RE::PlayerCharacter::GetSingleton()) {
				REX::LogError("Despawn: refusing to disable the local player"sv);
			} else {
				ref->Disable();
				ref->SetDelete(true);
			}
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
