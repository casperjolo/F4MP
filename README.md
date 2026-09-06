# F4MP

An actual attempt at making Fallout 4 multiplayer, in the spirit of [NV:MP](https://nv-mp.com/):
a dedicated server you host, a client plugin that loads through F4SE, and other players walking
around in your Commonwealth.

> **Status: pre-alpha vertical slice.** Players connect to a server, see each other move
> (with sneak / weapon-drawn / sprint state and their name on the crosshair), and chat through
> the console. NPC/combat/inventory/appearance sync is not there yet. See [ROADMAP.md](ROADMAP.md).

## How it works

```
 Fallout4.exe (+F4SE)             Fallout4.exe (+F4SE)
 ┌──────────────────┐             ┌──────────────────┐
 │ F4MP.dll (client)│◄──ENet/UDP─►│   F4MPServer.exe │◄──ENet/UDP─► more clients
 └──────────────────┘             └──────────────────┘
```

* **client/** – an F4SE plugin (`F4MP.dll`) built on [CommonLibF4 (AV fork)](https://github.com/LucaDotGit/CommonLibF4).
  Every frame it samples the local player, sends it to the server, and drives a clone actor per
  remote player with interpolated positions.
* **server/** – a portable C++ dedicated server (`F4MPServer.exe`). Authoritative player list, state
  relay, chat, console commands. No game files needed.
* **shared/** – the wire protocol used by both sides ([docs/PROTOCOL.md](docs/PROTOCOL.md)).

More detail in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Requirements (players)

| Component | Version |
|-----------|---------|
| Fallout 4 | 1.10.984 (next-gen) or 1.11.x — developed against **1.11.240** |
| [F4SE](https://f4se.silverlock.org/) | 0.7.2+ for 1.10.984, **0.7.9** for 1.11.240 |
| [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327) | database matching your game version |

Fallout 4 VR and the old 1.10.163 runtime are not supported.

## Install

1. Build (see [BUILDING.md](BUILDING.md)) or grab the `f4mp-client` artifact from CI.
2. Copy the contents of `dist/client/` into your Fallout 4 folder so you end up with
   `Data\F4SE\Plugins\F4MP.dll` and `Data\F4SE\Plugins\F4MP.ini`.
3. Edit `F4MP.ini`: set `Host`/`Port` to the server, optionally a `Name`.
4. Launch the game through `f4se_loader.exe`. Load a save or start a new game; the plugin
   connects automatically and reports in the console (`~`) and in `Documents\My Games\Fallout4\F4SE\F4MP.log`.

### In-game console

Open the console with `~` and use the `f4mp` command:

| Command | What it does |
|---------|--------------|
| `f4mp status` | Connection state, server, your id, ping, player count |
| `f4mp connect [host[:port]]` | Connect, optionally to a different server than `F4MP.ini` says |
| `f4mp disconnect` | Leave; auto-connect stays off until `f4mp connect` or the next save load |
| `f4mp say <text>` | Chat (no quotes needed) |
| `f4mp list` | Everyone on the server and whether they are in view |
| `f4mp name <name>` | Change your name; applies on the next connect |
| `f4mp help` | This list |

## Host a server

1. Copy `dist/server/` anywhere and edit `server.ini` (port, name, MOTD, max players).
2. Run `F4MPServer.exe`. Forward the UDP port for friends outside your LAN.
3. Console: `help`, `list`, `say <text>`, `kick <id> [reason]`, `stop`.

## Repository layout

```
client/     F4SE plugin sources and default F4MP.ini
server/     dedicated server sources and default server.ini
shared/     protocol headers shared by client and server
lib/        CommonLibF4 (git submodule)
docs/       architecture and protocol notes
```

## License

MIT — see [LICENSE](LICENSE). CommonLibF4 is MIT licensed by its authors.
