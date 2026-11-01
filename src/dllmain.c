#include <windows.h>
#include "noclip.h"

static DWORD WINAPI init_thread(LPVOID h)
{
    nc_init((HINSTANCE)h);
    if (nc_install()) {
        nc_input_install();
        nc_hud_install();
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(NULL, 0, init_thread, h, 0, NULL);
    }
    return TRUE;
}
