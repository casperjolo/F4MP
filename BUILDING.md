# Building F4MP

Both the client plugin and the server build from the one CMake tree. Windows only for the client
(it is a DLL that loads into Fallout4.exe); the server is portable C++ but the presets below target
Windows.

## Prerequisites

| Tool | Notes |
|------|-------|
| Visual Studio 2026 or 2022 17.14 (Build Tools are enough) | "Desktop development with C++" workload, Windows 11 SDK. CommonLibF4 targets the VS 2026 STL; VS 2022 17.14 (MSVC 14.44) works because `client/compat/` supplies empty stand-ins for the two C++23 headers it lacks (`<flat_map>`, `<flat_set>`), which the fork includes but never uses. Older toolsets are untested. The Ninja presets use whichever `cl.exe` is on `PATH`. |
| CMake 4.3+ | https://cmake.org/download/ (or `winget install Kitware.CMake`). The VS *Build Tools* only bundle CMake/Ninja when the "C++ CMake tools for Windows" component is ticked, and that CMake is usually older than 4.3 anyway. |
| Ninja | `winget install Ninja-build.Ninja` (or the VS CMake component above) |
| vcpkg | `git clone https://github.com/microsoft/vcpkg C:\vcpkg`, run `bootstrap-vcpkg.bat`, set `VCPKG_ROOT=C:\vcpkg` as a user environment variable and open a new prompt. Note that the VS developer prompt overrides `VCPKG_ROOT` with the copy bundled in Visual Studio (`...\VC\vcpkg`); that copy works too because `vcpkg.json` pins a `builtin-baseline`, but to reuse the `C:\vcpkg` clone and its port tree run `set VCPKG_ROOT=C:\vcpkg` after opening the prompt. |
| git | with submodule support |

Dependencies (`fmt`, `spdlog`, `enet`) are pulled by vcpkg manifest mode on first configure.

## Clone

```bash
git clone --recurse-submodules https://github.com/casperjolo/F4MP.git
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build

Open an **x64 Native Tools Command Prompt** (or run `vcvars64.bat`) so `cl.exe` is on `PATH`, then:

```bash
cmake --preset ninja-release
```

```bash
cmake --build --preset ninja-release
```

Output lands in `build/ninja-release/dist/`:

```
dist/client/Data/F4SE/Plugins/F4MP.dll   (+ .pdb, F4MP.ini)
dist/server/F4MPServer.exe               (+ .pdb, server.ini)
```

Other presets: `ninja-debug`, and `vs2026` (generates `build/vs2026/F4MP.sln` for Visual Studio 2026).

### Copy straight into the game

Set `XSE_FO4_GAME_PATH` to your Fallout 4 folder before configuring and every build copies
`F4MP.dll` + `.pdb` into `Data\F4SE\Plugins` automatically:

```powershell
$env:XSE_FO4_GAME_PATH = "C:\Program Files (x86)\Steam\steamapps\common\Fallout 4"
```

(`XSE_FO4_MODS_PATH` works the same way for a Mod Organizer 2 `mods` folder.)

### Server only / client only

```bash
cmake --preset ninja-release -DF4MP_BUILD_CLIENT=OFF
```

## Continuous integration

`.github/workflows/build.yml` builds both targets on every push and uploads `f4mp-client` and
`f4mp-server` artifacts, so you can grab binaries without a local toolchain.

## Troubleshooting

* **`Could not find a package configuration file provided by "unofficial-enet"`** – `VCPKG_ROOT`
  is not set or the preset did not pick up the toolchain file. Re-run configure from a fresh
  build directory.
* **`CommonLibF4 submodule is missing`** – run `git submodule update --init --recursive`.
* **`this vcpkg instance requires a manifest with a specified baseline`** – `VCPKG_ROOT` points at the
  vcpkg bundled with Visual Studio and `vcpkg.json` has lost its `builtin-baseline`. Put it back with the
  commit from `git -C C:\vcpkg rev-parse HEAD`, or point `VCPKG_ROOT` at `C:\vcpkg`.
* **`rc.exe` / `mt.exe` not found while vcpkg builds a port** – the Windows SDK is incomplete. In the
  developer prompt `set WindowsSDKVersion` must print a version, and `where rc` a path under
  `Windows Kits\10\bin`. Otherwise re-run the Visual Studio Installer and add "Windows 11 SDK".
* **Plugin does not load in game** – check `Documents\My Games\Fallout4\F4SE\f4se.log`. The usual
  causes are a missing Address Library database for your game version or an F4SE/game mismatch.
* **`cmake` / `ninja` is not recognized** – they are not on `PATH`. Install them (table above) and
  open a fresh developer prompt; `where cmake` should print a path.
