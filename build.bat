@echo off
rem Build Noclip for BFBC2 (32-bit) from a Windows command prompt. Same commands as build.sh.
rem Needs Zig 0.16.0 from PyPI:  python -m pip install ziglang==0.16.0
rem Output: build\Noclip.dll and build\NoclipLauncher.exe, also copied into dist\
rem Another compiler: set CC=i686-w64-mingw32-gcc before running (builds fine, but not byte-comparable).
setlocal
cd /d "%~dp0"
if not defined CC set "CC=python -m ziglang cc -target x86-windows-gnu"
if not exist build mkdir build
%CC% -shared -O2 -Wall -o build/Noclip.dll src/dllmain.c src/noclip.c src/freecam.c src/extras.c src/hud.c src/input.c || exit /b 1
%CC% -O2 -Wall -o build/NoclipLauncher.exe src/inject.c || exit /b 1
copy /y build\Noclip.dll dist\ >nul
copy /y build\NoclipLauncher.exe dist\ >nul
echo built build\Noclip.dll build\NoclipLauncher.exe (copied to dist\)
