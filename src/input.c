/* While flying, the game must not see the flight keys: otherwise the soldier (left behind in ghost mode)
 * tries to walk, jump, crouch and sprint in place - footstep and movement sounds - and mouse buttons would
 * fire from the body. BFBC2 reads keyboard and mouse through DirectInput8 (its only DINPUT8 import is
 * DirectInput8Create), so IDirectInputDevice8::GetDeviceState / GetDeviceData are filtered: flight keys
 * read as released and mouse buttons as up. Mouse movement (looking around) passes untouched. The mod's
 * own key reading uses GetAsyncKeyState and is not affected. Nothing is filtered when noclip is off. */
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>
#include <string.h>
#include <stdio.h>
#include "noclip.h"

typedef HRESULT(STDMETHODCALLTYPE *getstate_fn)(IDirectInputDevice8A *, DWORD, LPVOID);
typedef HRESULT(STDMETHODCALLTYPE *getdata_fn)(IDirectInputDevice8A *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
typedef HRESULT(WINAPI *di8create_fn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

static getstate_fn o_getstate;
static getdata_fn o_getdata;
static unsigned char g_block[256];  /* DIK scan codes to hide */
static int block_mouse = 1, block_enabled = 1;
static volatile int g_release_pending;
static int blocking(void);
volatile int nc_mouse_capture;
static volatile LONG g_mx, g_my; /* mouse counts taken from the game for the free camera */

void nc_mouse_take(int *dx, int *dy)
{
    *dx = InterlockedExchange(&g_mx, 0);
    *dy = InterlockedExchange(&g_my, 0);
}
static int g_logged_state, g_logged_data;

/* device type cache: keyboard / mouse / other, by device pointer */
#define MAXDEV 8
static struct { void *dev; int type; } g_dev[MAXDEV];

static int dev_type(IDirectInputDevice8A *d)
{
    int i;
    DIDEVCAPS caps;
    for (i = 0; i < MAXDEV; i++)
        if (g_dev[i].dev == d) return g_dev[i].type;
    memset(&caps, 0, sizeof caps);
    caps.dwSize = sizeof caps;
    int t = 0;
    if (SUCCEEDED(IDirectInputDevice8_GetCapabilities(d, &caps)))
        t = GET_DIDEVICE_TYPE(caps.dwDevType);
    for (i = 0; i < MAXDEV; i++)
        if (!g_dev[i].dev) { g_dev[i].dev = d; g_dev[i].type = t; break; }
    return t;
}

static void build_block_list(void)
{
    int keys[16], n = nc_flight_keys(keys, 16), i;
    memset(g_block, 0, sizeof g_block);
    for (i = 0; i < n; i++) {
        UINT sc = MapVirtualKeyA(keys[i], 0 /* MAPVK_VK_TO_VSC */);
        if (sc && sc < 256) g_block[sc] = 1;
        /* generic modifiers: hide both left and right keys */
        if (keys[i] == VK_CONTROL) { g_block[DIK_LCONTROL] = g_block[DIK_RCONTROL] = 1; }
        if (keys[i] == VK_SHIFT) { g_block[DIK_LSHIFT] = g_block[DIK_RSHIFT] = 1; }
        if (keys[i] == VK_MENU) { g_block[DIK_LMENU] = g_block[DIK_RMENU] = 1; }
    }
}

static HRESULT STDMETHODCALLTYPE h_getstate(IDirectInputDevice8A *d, DWORD cb, LPVOID data)
{
    HRESULT hr = o_getstate(d, cb, data);
    if (!g_logged_state) { g_logged_state = 1; nc_log("input: game uses GetDeviceState (cb=%lu)", cb); }
    if (SUCCEEDED(hr) && data && nc_mouse_capture && (cb == sizeof(DIMOUSESTATE) || cb == sizeof(DIMOUSESTATE2)) &&
        dev_type(d) == DI8DEVTYPE_MOUSE) {
        DIMOUSESTATE *ms = (DIMOUSESTATE *)data; /* lX/lY lead both layouts */
        InterlockedExchangeAdd(&g_mx, ms->lX);
        InterlockedExchangeAdd(&g_my, ms->lY);
        ms->lX = ms->lY = 0;
    }
    if (FAILED(hr) || !blocking() || !data)
        return hr;
    if (cb == 256 && dev_type(d) == DI8DEVTYPE_KEYBOARD) {
        unsigned char *k = (unsigned char *)data;
        int i;
        for (i = 0; i < 256; i++)
            if (g_block[i]) k[i] = 0;
    } else if (block_mouse && (cb == sizeof(DIMOUSESTATE) || cb == sizeof(DIMOUSESTATE2)) && dev_type(d) == DI8DEVTYPE_MOUSE) {
        if (cb == sizeof(DIMOUSESTATE)) memset(((DIMOUSESTATE *)data)->rgbButtons, 0, 4);
        else memset(((DIMOUSESTATE2 *)data)->rgbButtons, 0, 8);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE h_getdata(IDirectInputDevice8A *d, DWORD cbObj, LPDIDEVICEOBJECTDATA rg, LPDWORD inout, DWORD flags)
{
    DWORD cap = inout ? *inout : 0;
    HRESULT hr = o_getdata(d, cbObj, rg, inout, flags);
    if (!g_logged_data) { g_logged_data = 1; nc_log("input: game uses GetDeviceData (buffered)"); }
    if (FAILED(hr) || !rg || !inout || (flags & DIGDD_PEEK))
        return hr;
    int type = dev_type(d);
    DWORD i, n = *inout;
    if (nc_mouse_capture && type == DI8DEVTYPE_MOUSE) {
        for (i = 0; i < n; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((char *)rg + i * cbObj);
            if (e->dwOfs == DIMOFS_X) { InterlockedExchangeAdd(&g_mx, (LONG)e->dwData); e->dwData = 0; }
            else if (e->dwOfs == DIMOFS_Y) { InterlockedExchangeAdd(&g_my, (LONG)e->dwData); e->dwData = 0; }
        }
    }
    if (blocking()) {
        for (i = 0; i < n; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((char *)rg + i * cbObj);
            if (type == DI8DEVTYPE_KEYBOARD && e->dwOfs < 256 && g_block[e->dwOfs])
                e->dwData = 0; /* every press becomes a release */
            else if (type == DI8DEVTYPE_MOUSE && block_mouse && e->dwOfs >= DIMOFS_BUTTON0 && e->dwOfs <= DIMOFS_BUTTON7)
                e->dwData = 0;
        }
    }
    /* keys already held when F6 was pressed: send the game a release for each one, once */
    if (g_release_pending && type == DI8DEVTYPE_KEYBOARD && blocking()) {
        int k;
        for (k = 0; k < 256 && n < cap; k++) {
            if (!g_block[k]) continue;
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((char *)rg + n * cbObj);
            memset(e, 0, cbObj);
            e->dwOfs = k;
            e->dwData = 0;
            e->dwTimeStamp = GetTickCount();
            e->dwSequence = n ? ((DIDEVICEOBJECTDATA *)((char *)rg + (n - 1) * cbObj))->dwSequence : 0;
            n++;
        }
        *inout = n;
        g_release_pending = 0;
    }
    return hr;
}

/* called by the mode switch (main thread) */
void nc_input_on_toggle(int on)
{
    if (on) {
        block_enabled = GetPrivateProfileIntA("input", "block_game_input_while_flying", 1, nc_ini());
        block_mouse = GetPrivateProfileIntA("input", "block_mouse_buttons", 1, nc_ini());
        build_block_list();
        g_release_pending = block_enabled;
    }
}

static int blocking(void) { return nc_input_block && block_enabled; }

static void patch(void **vt, int idx, void *fn, void **orig)
{
    DWORD old;
    VirtualProtect(&vt[idx], 4, PAGE_READWRITE, &old);
    *orig = vt[idx];
    vt[idx] = fn;
    VirtualProtect(&vt[idx], 4, old, &old);
}

static const GUID IID_DI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID GUID_Kbd = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

int nc_input_install(void)
{
    HMODULE di = GetModuleHandleA("dinput8.dll");
    di8create_fn create = di ? (di8create_fn)GetProcAddress(di, "DirectInput8Create") : NULL;
    IDirectInput8A *dinput = NULL;
    IDirectInputDevice8A *kbd = NULL;
    block_enabled = GetPrivateProfileIntA("input", "block_game_input_while_flying", 1, nc_ini());
    block_mouse = GetPrivateProfileIntA("input", "block_mouse_buttons", 1, nc_ini());
    if (!create || FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_DI8A, (void **)&dinput, NULL)) ||
        FAILED(IDirectInput8_CreateDevice(dinput, &GUID_Kbd, &kbd, NULL))) {
        nc_log("input: DirectInput8 not available; game input is not filtered");
        if (dinput) IDirectInput8_Release(dinput);
        return 0;
    }
    void **vt = *(void ***)kbd;
    if (vt[9] == (void *)h_getstate) { /* already ours (dev reload) */
        IDirectInputDevice8_Release(kbd);
        IDirectInput8_Release(dinput);
        return 1;
    }
    patch(vt, 9, (void *)h_getstate, (void **)&o_getstate);
    patch(vt, 10, (void *)h_getdata, (void **)&o_getdata);
    nc_log("input: hooked IDirectInputDevice8 vtable %p (GetDeviceState %p, GetDeviceData %p)", vt, o_getstate, o_getdata);
    IDirectInputDevice8_Release(kbd);
    IDirectInput8_Release(dinput);
    build_block_list();
    return 1;
}
