# Architecture

## Components

| Component | Language / deps | Role |
|-----------|-----------------|------|
| `client/` → `F4MP.dll` | C++23, CommonLibF4 (AV fork), ENet | F4SE plugin loaded into Fallout4.exe. Samples the local player, talks to the server, drives remote player actors. |
| `server/` → `F4MPServer.exe` | C++23, ENet, spdlog | Authoritative lobby: assigns ids, relays state, chat, kicks. Needs no game files. |
| `shared/` | header-only | Message ids, serialization, INI parsing. |

## Client

### Lifecycle

1. `F4SEPlugin_Load` (`Main.cpp`): `F4SE::Init`, read `F4MP.ini`, register the F4SE message
   listener and a **permanent task** (`AddTaskPermanent`) that runs `Session::Tick` every frame on
   the game thread.
2. `kPostLoadGame` / `kNewGame` → `Session::OnEnterWorld`: the player exists, autoconnect kicks in.
3. `kPreLoadGame` → `Session::OnLeaveWorld`: forget actor handles (the world is going away).
4. `kPreSaveGame` → `Session::OnPreSave`: delete clone actors so they never end up in a save.

### Per frame (`Session::Tick`)

```
NetClient::Pump()                 service ENet with 0 timeout, dispatch packets
if in world and disconnected      reconnect every 10 s
if connected and welcomed:
    SendState()                   sample player, send at SendRate Hz when changed (1 Hz heartbeat)
    RemotePlayers::Update()       spawn / despawn / interpolate every remote player
```

Everything is single-threaded on the game thread, so engine calls need no locking.

### Remote players

Each remote player gets its own **runtime copy of the player base NPC** (form `0x00000007`,
duplicated with `TESForm::CreateDuplicateForm`). The copy carries the remote player's name (so it
shows on the crosshair), is flagged *ghost* (combat AI ignores it, it takes no damage), loses
*unique/essential/protected*, and has aggression and assistance set to none. An actor of that
base is placed with `TESDataHandler::CreateReferenceAtLocation` only while the remote player is
in the same worldspace (exterior) or the same cell (interior) as the local player, and is
despawned when either of them moves elsewhere. Once the actor's AI process exists it is given
the *do nothing* package and taken out of combat, so no package ever fights the network.

Dynamic forms do not survive loading a save, so `ForgetActors()` drops both actor handles and
base-form pointers on `kPreLoadGame` / `kPostLoadGame`; they are recreated on demand.

Movement uses the last two snapshots and renders `InterpDelayMs` in the past
(`Actor::SetPosition` + `Actor::SetHeading`). Death/resurrection follows the `kStateDead` flag;
sneaking, weapon drawn and sprinting are pushed through `Actor::SetSneaking`,
`Actor::DrawWeaponMagicHands` and the `ActorState::sprinting` bit whenever a flag changes.

### Console command

`Game/ConsoleCommands.cpp` takes over the table entry of the stock debug command `TestSeenData`
(`SCRIPT_FUNCTION::GetConsoleFunctionByName`) and renames it `f4mp`. The entry keeps one optional
string parameter so the script compiler accepts a sub-command, but the handler ignores the compiled
parameters and re-parses the raw line from `Script::GetText()`; that is what lets
`f4mp say hello there` work without quotes.

### Versioning

`PluginInfo.cpp` declares the Address Library + layout flags for both NG (1.10.984) and AE
(1.11.x). F4SE treats such a plugin as version independent, so it loads on 1.11.240 even though
CommonLibF4's own constant for the latest AE runtime is 1.11.191. All engine addresses resolve at
runtime through the Address Library database for the running version.

## Server

Single-threaded loop: `enet_host_service` with a timeout derived from `TickRate`, then drain the
console command queue (stdin is read on a helper thread). Player records are keyed by ENet peer.

Flow for a new connection: `ENET_EVENT_TYPE_CONNECT` → wait for `Hello` → validate protocol
version → assign id → `Welcome` → send the existing player list (and their last state) to the
newcomer → broadcast `PlayerJoined`. `PlayerState` packets are sanity-checked (finite floats),
stored and relayed to everyone else immediately on the unreliable channel. Chat is length-capped
and flood-limited per player (see [PROTOCOL.md](PROTOCOL.md)).

## Networking

ENet over UDP with two channels:

* **0 reliable** – handshake, join/leave, chat, kicks.
* **1 unreliable (sequenced)** – player state. Stale packets are dropped by ENet, so clients
  always see the newest snapshot.

Serialization is a hand-rolled little-endian writer/reader (`shared/include/f4mp/Packet.hpp`);
message layouts are in [PROTOCOL.md](PROTOCOL.md).
