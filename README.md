# Noclip / Free Camera for Battlefield: Bad Company 2

Full source code of the mod **Noclip & Free Camera** for Battlefield: Bad Company 2 (PC), published on Nexus
Mods: https://www.nexusmods.com/battlefieldbadcompany2/mods/66

This repository is version **2.0.0**, the file uploaded to Nexus (`BFBC2-Noclip-FreeCamera-2.0.0.zip`). Building
it gives the same `Noclip.dll` and `NoclipLauncher.exe` as in that archive (see [Checking the build](#checking-the-build)).

## What the mod does

Single-player campaign only.

- **F6**: free camera. Fly through walls, terrain and buildings, also during in-engine cutscenes.
- **F8**: pause and resume the game, cutscenes included, without the pause menu.
- The HUD is hidden while the camera is on, for clean screenshots.
- The soldier stays where the flight started, so flying around does not set off story triggers.
- No game files and no saves are changed. Deleting the `Noclip` folder removes the mod completely.

Player documentation: [dist/README_EN.txt](dist/README_EN.txt) (Russian: [dist/README_RU.txt](dist/README_RU.txt)).

## What is in the release archive

| File | Source | What it is |
|---|---|---|
| `Noclip.dll` | `src/dllmain.c`, `noclip.c`, `freecam.c`, `extras.c`, `hud.c`, `input.c`, `game.h`, `noclip.h` | the mod, loaded into `BFBC2Game.exe` |
| `NoclipLauncher.exe` | `src/inject.c` | starts the game (or finds it running) and loads `Noclip.dll` into it |
| `Noclip-Attach.bat` | `dist/` | runs `NoclipLauncher.exe --attach` for a game that is already running |
| `Noclip.ini` | `dist/` | keys and settings |
| `README_EN.txt`, `README_RU.txt`, `CHANGELOG.txt`, `LICENSE.txt` | `dist/` | documentation, MIT license |

After a build, `dist/` holds exactly these files.

## How it works

**NoclipLauncher.exe** (`src/inject.c`, 118 lines) loads one DLL into the game with the standard
`LoadLibraryA` method:

1. Finds a running `BFBC2Game.exe`, or starts `..\BFBC2Game.exe` (the game folder above the `Noclip` folder).
2. Refuses to continue if PunkBuster (`pbcl.dll`) is loaded, that is in multiplayer.
3. `OpenProcess`, `VirtualAllocEx` + `WriteProcessMemory` of the path of `Noclip.dll` (next to the launcher), then
   `CreateRemoteThread` on `LoadLibraryA`, and exits.

It opens no other process, writes no files and does not change the registry. Antivirus programs often flag this
pattern (`CreateRemoteThread` + `LoadLibraryA`) heuristically; it is how most DLL mods for this game are loaded.

**Noclip.dll** imports only `KERNEL32`, `USER32` and the C runtime: no network, no registry access. It reads
`Noclip.ini` and writes `Noclip.log`, both in its own folder. Everything else happens inside the game's memory:

- **Version check** (`noclip.c`, `nc_install`): compares a few bytes of four game functions with build 795745
  (FileVersion 1.0.1.0, the last patch). On any other build it logs "unsupported BFBC2Game.exe build" and hooks
  nothing.
- **Single-player gate** (`noclip.c`): redirects the call to the local server's update job, so the mod knows a
  single-player server is running. Without it F6 and F8 do nothing. With PunkBuster loaded the DLL stays off.
- **Free camera** (`freecam.c`): redirects one `call` instruction in the game's camera manager (`0x7A5E07`); while
  the camera is on, the game's camera transform is replaced by the free one.
- **The soldier while flying** (`noclip.c`): chains one entry of the server soldier's virtual table (slot 71, the
  per-tick update) to hold the body in place and put it down when the flight ends.
- **Input** (`input.c`): wraps `GetDeviceState`/`GetDeviceData` of the game's DirectInput 8 devices, so the game
  does not see the flight keys while flying.
- **HUD** (`hud.c`): creates a hidden 64x64 window and a temporary Direct3D 11 device once to find the address of
  `IDXGISwapChain::Present`, then wraps `Present`, `OMSetRenderTargets` and the four `Draw*` functions of the game's
  device context, and skips the 2D UI draws while flying.
- **Optional engine settings** (`extras.c`): engine switches listed by name in `Noclip.ini` `[while_noclip]` (empty
  by default) are set while flying and restored afterwards.

All game addresses and offsets are in `src/game.h`, with comments. They were found by reverse engineering
`BFBC2Game.exe` (build 795745) and checked in game. The repository contains no game files and no decompiled code.

## Building

The release was built with **Zig 0.16.0** used as a C compiler (`zig cc`, clang + MinGW-w64 headers and libraries).
Zig is installed from PyPI with pip; no Visual Studio is needed.

1. Install **Python 3** for Windows from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
2. Open a Command Prompt and install the compiler:

   ```
   python -m pip install ziglang==0.16.0
   ```

3. Get the source: `git clone` this repository, or download it as ZIP and extract it.
4. In the source folder run:

   ```
   build.bat
   ```

   (or `./build.sh` in Git Bash / MSYS2). The first run takes about a minute while Zig prepares its C runtime.
5. Result: `build\Noclip.dll` and `build\NoclipLauncher.exe`, also copied into `dist\`.
   `dist\` then contains the complete release; the release archive is that folder renamed to `Noclip` and zipped.

The two compiler commands the scripts run:

```
python -m ziglang cc -target x86-windows-gnu -shared -O2 -Wall -o build/Noclip.dll src/dllmain.c src/noclip.c src/freecam.c src/extras.c src/hud.c src/input.c
python -m ziglang cc -target x86-windows-gnu -O2 -Wall -o build/NoclipLauncher.exe src/inject.c
```

Any 32-bit MinGW-w64 GCC also builds it (`set CC=i686-w64-mingw32-gcc`, then `build.bat`), but only Zig 0.16.0
gives binaries that can be compared with the release.

## Checking the build

With Zig 0.16.0 both files come out the same as in the uploaded archive: same size, and every section (code,
data, imports, relocations) byte-identical. Only 20 bytes per file differ: the link time stamps (PE header and debug
directory) and the build ID of the debug record, which the linker writes anew on every build.

```
python -m pip install pefile
python tools/compare_pe.py build/Noclip.dll path\to\released\Noclip.dll
python tools/compare_pe.py build/NoclipLauncher.exe path\to\released\NoclipLauncher.exe
```

Each command ends with `SAME CODE AND DATA`.

SHA-256 of the uploaded files:

| File | SHA-256 |
|---|---|
| `BFBC2-Noclip-FreeCamera-2.0.0.zip` (300 901 bytes) | `e750db9fc39c53338cc31bc14e1d0458e781835aad845ed6ea477b15e433c1fe` |
| `Noclip/Noclip.dll` (328 704 bytes) | `55055a77c20295e6eeb23a3ed4b6cc5e3b4864c93442384f24ff0b533720f045` |
| `Noclip/NoclipLauncher.exe` (243 712 bytes) | `8341c4cb095275a407b970780a773ca7b16b6399cdabac963ad92769c665811d` |

## Installing

Extract the release archive into the game folder (where `BFBC2Game.exe` is) and start the game with
`Noclip\NoclipLauncher.exe`, or start the game as usual and run `Noclip\Noclip-Attach.bat`. Details, controls and
limitations: [dist/README_EN.txt](dist/README_EN.txt).

## License and credits

MIT, see [LICENSE](LICENSE). Written with the AI coding agent Claude Code (Anthropic), directed and tested in game by
the author. Battlefield: Bad Company 2 belongs to EA/DICE; this repository contains no game files.
