# Roadmap

The target is the NV:MP experience on Fallout 4: a persistent server-hosted world where every
player is a real actor, NPCs are shared, and the server is authoritative. Phases roughly in order.

## Phase 0 – Vertical slice (current)

- [x] F4SE plugin skeleton on CommonLibF4 AV (1.10.984 + 1.11.x in one DLL)
- [x] Dedicated server with ENet, config, console commands
- [x] Handshake, join/leave, player state relay, chat (server → console)
- [x] Remote players as player-clone actors with interpolated position/heading
- [x] `f4mp` console command (status / connect / disconnect / say / list / name)
- [x] First in-game test on 1.11.240 (2026-09-06, F4SE 0.7.9, VS 2022 17.14 build): plugin loads,
      connects, receives welcome/MOTD/chat, `f4mp status` works
- [x] Address Library IDs used at load/connect resolve on 1.11.240 (clone-spawn paths not exercised yet)
- [x] `TestSeenData` console slot takeover works
- [ ] Unquoted multi-word `f4mp say hello there`
- [ ] Two clients on one server: clone spawns, follows movement, shows the remote name

## Phase 1 – Players that look and act like players

- [x] Remote name on the crosshair (per-player copy of the player NPC carries the name)
- [ ] Floating name plates above remote players (Scaleform HUD widget)
- [ ] Appearance sync (face morphs, race, sex, hair) via the per-player TESNPC copy
- [ ] Equipment and weapon sync (armor, held weapon, power armor)
- [x] Animation state: sneaking, sprinting, weapon drawn (untested in-game)
- [ ] Animation state: aiming, jumping, proper death ragdoll
- [x] Clones off the AI: ghost base, no aggression, "do nothing" package (untested in-game)
- [x] Chat input via console command `f4mp say`
- [ ] Chat input UI (Scaleform text box, chat history overlay)
- [ ] Reconnect / server browser in a small launcher (like NV:MP's)

## Phase 2 – Shared world

- [ ] NPC sync with ownership (nearest player simulates, server arbitrates)
- [ ] Combat: hits, damage, death events forwarded through the server
- [ ] Doors, containers, dropped items, world object state
- [ ] Time of day / weather sync
- [ ] Server-side player persistence (position, inventory) instead of local saves

## Phase 3 – Server platform

- [ ] Server scripting API (C# or Lua) for game modes and admin tools, à la NV:MP server mods
- [ ] Voice chat
- [ ] Anti-cheat basics: server-side validation, rate limits, bans (so far: NaN/inf state rejection, chat flood control)
- [ ] Linux server builds
