NOCLIP / FREE CAMERA for Battlefield: Bad Company 2 — version 2.0.0
===================================================================

WHAT IT DOES
- F6: free camera. Fly through walls, terrain and buildings in the single-player campaign.
- Works DURING CUTSCENES too: fly around the characters and watch the scene from any angle.
- F8: pauses the game, cutscenes included, without the pause menu. The scene freezes (characters,
  water spray, smoke, sound) while the camera keeps flying. F8 again resumes from the same moment.
- The HUD is hidden while the camera is on, for clean screenshots.
- Your soldier stays where it was, so flying around does not set off story triggers.
- No game files or saves are changed. Single player only.

REQUIREMENTS
- Battlefield: Bad Company 2 for PC, latest patch: BFBC2Game.exe file version 1.0.1.0 (build 795745).
  Check: right-click BFBC2Game.exe → Properties → Details → File version.
  On any other version the mod does nothing and says so in Noclip\Noclip.log.
- Windows, DirectX 11 (on DX9/DX10 everything works except hiding the HUD).
- Nothing else to install.

INSTALL
1. Extract the archive into the game folder (where BFBC2Game.exe is).
   You should end up with: <game folder>\Noclip\NoclipLauncher.exe
2. Start the game with Noclip\NoclipLauncher.exe.
   If the game is already running (or the launcher does not work, e.g. Steam / EA app): start the
   game as usual, then double-click Noclip\Noclip-Attach.bat.
3. Start or continue the campaign.

UNINSTALL
Delete the Noclip folder. Nothing else is left behind.

CONTROLS
  F6 ............ free camera on/off
  F8 ............ pause / resume (cutscenes too)
  Mouse ......... look
  W/S ........... forward / back along the view
  A/D ........... left / right
  Space/Ctrl .... up / down
  Shift ......... faster (x5)
  Alt ........... slower (x0.25)
  Backspace ..... camera off and put the soldier back where the flight started
Keys, speed, mouse sensitivity and invert-Y are in Noclip\Noclip.ini (Windows virtual-key codes,
listed at the top of the file).

EXPLORING A CUTSCENE
  wait for the moment → F8 (frozen) → F6 (your camera) → fly →
  F6 (back to the scene's shot) → F8 (the scene goes on)
Without F8 the scene keeps playing while you fly around it.

WHAT HAPPENS TO YOUR SOLDIER
- The camera flies; the soldier stays where you pressed F6.
- On F6 off the soldier is put down under the camera: on the ground, or above it if inside a wall.
- Backspace instead of F6 puts the soldier back where you started.
- If a cutscene happened during the flight, the soldier is NOT moved, so the script cannot break.

LIMITATIONS
- Single player only. In multiplayer F6 and F8 do nothing (the mod checks that the single-player
  local server is running).
- The F8 pause stays on until you press F8 again. If the game looks frozen after loading, press F8.
- F8 is the same pause the Esc menu uses: opening and closing the menu resumes the game. While a menu
  is open the mod's keys do nothing and the camera holds still.
- With the camera on and the game not paused, enemies can shoot your soldier.
- Pre-rendered videos (Bink) cannot be explored, only in-engine scenes.
- The first-person arms and weapon stay with the soldier and are not visible while flying.

TROUBLESHOOTING
- Look at Noclip\Noclip.log. "unsupported BFBC2Game.exe build" means a different game version.
- Antivirus may flag NoclipLauncher.exe: it loads a DLL into the game process (as most DLL mods
  do). The source code is open, you can build it yourself.
- With Steam / EA app the launcher may end up restarting the game: use Noclip-Attach.bat instead.

CREDITS AND LICENSE
Code: MIT (LICENSE.txt). Built with the AI coding agent Claude Code (Anthropic's Claude Opus 5.5
model), directed and tested in game by the author; engine addresses were found by reverse
engineering and verified in game. Battlefield: Bad Company 2 belongs to EA/DICE; this mod contains
no game files.
