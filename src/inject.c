/* Noclip launcher/injector (x86). Starts BFBC2Game.exe from its folder (or finds it running) and loads
 * a DLL into it with LoadLibraryA. No game file is modified and no proxy DLL is used.
 *   NoclipLauncher.exe [--attach] [path\to\Noclip.dll]
 * Default DLL: Noclip.dll next to this exe. Default game: ..\BFBC2Game.exe relative to this exe. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static DWORD find_game(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    DWORD pid = 0;
    pe.dwSize = sizeof pe;
    if (Process32First(snap, &pe)) {
        do {
            if (!_stricmp(pe.szExeFile, "BFBC2Game.exe")) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static int has_module(DWORD pid, const char *name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32 me;
    int found = 0;
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    me.dwSize = sizeof me;
    if (Module32First(snap, &me)) {
        do {
            if (!_stricmp(me.szModule, name))
                found = 1;
        } while (!found && Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

int main(int argc, char **argv)
{
    char self[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH], game[MAX_PATH], gamedir[MAX_PATH];
    int attach = 0, i;
    char *slash;

    GetModuleFileNameA(NULL, self, MAX_PATH);
    strcpy(dir, self);
    slash = strrchr(dir, '\\');
    if (slash)
        *slash = 0;
    snprintf(dll, MAX_PATH, "%s\\Noclip.dll", dir);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--attach"))
            attach = 1;
        else
            GetFullPathNameA(argv[i], MAX_PATH, dll, NULL);
    }
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "DLL not found: %s\n", dll);
        return 1;
    }

    DWORD pid = find_game();
    if (!pid && !attach) {
        strcpy(gamedir, dir);
        slash = strrchr(gamedir, '\\');
        if (slash)
            *slash = 0;
        snprintf(game, MAX_PATH, "%s\\BFBC2Game.exe", gamedir);
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (!CreateProcessA(game, NULL, NULL, NULL, FALSE, 0, NULL, gamedir, &si, &pi)) {
            fprintf(stderr, "cannot start %s (%lu)\n", game, GetLastError());
            return 1;
        }
        pid = pi.dwProcessId;
        WaitForInputIdle(pi.hProcess, 60000);
        Sleep(3000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (!pid) {
        fprintf(stderr, "BFBC2Game.exe is not running\n");
        return 1;
    }
    if (has_module(pid, "pbcl.dll")) {
        fprintf(stderr, "PunkBuster is loaded (multiplayer). Noclip is single-player only; not loading.\n");
        return 2;
    }
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) {
        fprintf(stderr, "OpenProcess failed (%lu)\n", GetLastError());
        return 1;
    }
    SIZE_T n = strlen(dll) + 1;
    LPVOID mem = VirtualAllocEx(h, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(h, mem, dll, n, NULL);
    LPTHREAD_START_ROUTINE ll = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE t = CreateRemoteThread(h, NULL, 0, ll, mem, 0, NULL);
    if (!t) {
        fprintf(stderr, "CreateRemoteThread failed (%lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(t, 15000);
    DWORD mod = 0;
    GetExitCodeThread(t, &mod);
    VirtualFreeEx(h, mem, 0, MEM_RELEASE);
    printf(mod ? "loaded %s into pid %lu at %08lX\n" : "LoadLibrary failed for %s (pid %lu)\n", dll, pid, mod);
    return mod ? 0 : 1;
}
