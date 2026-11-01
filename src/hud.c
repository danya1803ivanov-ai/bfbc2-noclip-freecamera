/* Clean screen while flying. BFBC2 draws its whole 2D UI (EA Apt: crosshair, ammo, minimap, markers,
 * hints, subtitles) straight into the swap-chain back buffer after the final full-screen composite of the
 * 3D frame. While noclip is on we let the first `keep_draws` back-buffer draws of each frame through (the
 * composite) and drop the rest of that frame's back-buffer draws. Nothing is unloaded or changed in the
 * game: switching noclip off simply stops dropping draws. When the server tick stops (pause menu,
 * loading) nothing is dropped, so menus stay visible. */
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "noclip.h"

typedef HRESULT(STDMETHODCALLTYPE *present_fn)(IDXGISwapChain *, UINT, UINT);
typedef void(STDMETHODCALLTYPE *omset_fn)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView *);
typedef void(STDMETHODCALLTYPE *draw_fn)(ID3D11DeviceContext *, UINT, UINT);
typedef void(STDMETHODCALLTYPE *drawidx_fn)(ID3D11DeviceContext *, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE *drawinst_fn)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
typedef void(STDMETHODCALLTYPE *drawidxinst_fn)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);

static present_fn o_present;
static omset_fn o_omset;
static draw_fn o_draw;
static drawidx_fn o_drawidx;
static drawinst_fn o_drawinst;
static drawidxinst_fn o_drawidxinst;

static volatile int g_hide;          /* noclip wants a clean screen */
static int hud_enabled = 1, keep_draws = 0, log_stats;
static ID3D11Resource *g_bb;         /* current back buffer */
static ID3D11RenderTargetView *g_last_rtv;
static ID3D11DeviceContext *g_ctx;
static int g_last_rtv_is_bb, g_bound_bb, g_in_present;
static unsigned g_bb_draws, g_dropped, g_frames;
static unsigned g_stat_max, g_stat_dropped;
static volatile int g_trace; /* dev: dump one frame of back-buffer draws */

static void trace(const char *kind, UINT n)
{
    if (g_trace != 2 || !g_bound_bb || g_in_present || !o_omset) return;
    {
        ID3D11DeviceContext *c = g_ctx;
        ID3D11VertexShader *vs = NULL; ID3D11PixelShader *ps = NULL;
        ID3D11DeviceContext_VSGetShader(c, &vs, NULL, NULL);
        ID3D11DeviceContext_PSGetShader(c, &ps, NULL, NULL);
        nc_log("trace bb#%u %s n=%u vs=%p ps=%p", g_bb_draws, kind, n, vs, ps);
        if (vs) ID3D11VertexShader_Release(vs);
        if (ps) ID3D11PixelShader_Release(ps);
    }
}

DWORD nc_last_server_tick; /* written by the server tick; 0 = never */

static ID3D11VertexShader *g_first_vs; /* vertex shader of this frame's first back-buffer draw */
static int g_ui_started;

/* keep_draws > 0: keep exactly that many back-buffer draws per frame.
   keep_draws = 0 (default): keep draws while they use the same vertex shader as the frame's first
   back-buffer draw (the 3D composite); the first draw with another shader starts the UI. Measured on
   build 795745 / DX11: draws 0-1 are the composite, 2-3 the minimap, 4-80 the rest of the HUD. */
static int dropping(void)
{
    unsigned i;
    if (!g_hide || g_in_present || !g_bound_bb)
        return 0;
    if (*(volatile DWORD *)G_MENU_INPUT) /* a menu has the input (pause menu, ...): show everything */
        return 0;
    if (GetTickCount() - nc_last_server_tick > 250 && !nc_paused_by_us) /* loading, other pauses */
        return 0;
    i = g_bb_draws++;
    if (keep_draws > 0)
        return i >= (unsigned)keep_draws;
    if (g_ui_started)
        return 1;
    {
        ID3D11VertexShader *vs = NULL;
        ID3D11DeviceContext_VSGetShader(g_ctx, &vs, NULL, NULL);
        if (vs) ID3D11VertexShader_Release(vs); /* only compared, never dereferenced */
        if (i == 0) { g_first_vs = vs; return 0; }
        if (vs == g_first_vs) return 0;
        g_ui_started = 1;
        return 1;
    }
}

static void count_only(void)
{
    if (g_bound_bb && !g_in_present) g_bb_draws++;
}

static void STDMETHODCALLTYPE h_omset(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv)
{
    g_bound_bb = 0;
    if (n && rtv && rtv[0] && g_bb) {
        if (rtv[0] != g_last_rtv) {
            ID3D11Resource *r = NULL;
            ID3D11View_GetResource((ID3D11View *)rtv[0], &r);
            g_last_rtv = rtv[0];
            g_last_rtv_is_bb = (r == g_bb);
            if (r) ID3D11Resource_Release(r);
        }
        g_bound_bb = g_last_rtv_is_bb;
    }
    o_omset(c, n, rtv, dsv);
}

#define DROP_OR_COUNT() do { if (g_hide) { if (dropping()) { g_dropped++; return; } } else count_only(); } while (0)

static void STDMETHODCALLTYPE h_draw(ID3D11DeviceContext *c, UINT a, UINT b) { trace("draw", a); DROP_OR_COUNT(); o_draw(c, a, b); }
static void STDMETHODCALLTYPE h_drawidx(ID3D11DeviceContext *c, UINT a, UINT b, INT d) { trace("idx", a); DROP_OR_COUNT(); o_drawidx(c, a, b, d); }
static void STDMETHODCALLTYPE h_drawinst(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, UINT e) { DROP_OR_COUNT(); o_drawinst(c, a, b, d, e); }
static void STDMETHODCALLTYPE h_drawidxinst(ID3D11DeviceContext *c, UINT a, UINT b, UINT d, INT e, UINT f) { DROP_OR_COUNT(); o_drawidxinst(c, a, b, d, e, f); }

static void patch(void **vt, int idx, void *fn, void **orig)
{
    DWORD old;
    VirtualProtect(&vt[idx], 4, PAGE_READWRITE, &old);
    *orig = vt[idx];
    vt[idx] = fn;
    VirtualProtect(&vt[idx], 4, old, &old);
}

static void hook_context(IDXGISwapChain *sc)
{
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    if (FAILED(IDXGISwapChain_GetDevice(sc, &IID_ID3D11Device, (void **)&dev))) {
        nc_log("hud: swap chain is not D3D11; HUD hiding unavailable");
        hud_enabled = 0;
        return;
    }
    ID3D11Device_GetImmediateContext(dev, &ctx);
    void **vt = *(void ***)ctx;
    g_ctx = ctx;
    patch(vt, 33, (void *)h_omset, (void **)&o_omset);
    patch(vt, 13, (void *)h_draw, (void **)&o_draw);
    patch(vt, 12, (void *)h_drawidx, (void **)&o_drawidx);
    patch(vt, 21, (void *)h_drawinst, (void **)&o_drawinst);
    patch(vt, 20, (void *)h_drawidxinst, (void **)&o_drawidxinst);
    nc_log("hud: hooked immediate context %p (vtable %p)", ctx, vt);
    ID3D11DeviceContext_Release(ctx);
    ID3D11Device_Release(dev);
}

static HRESULT STDMETHODCALLTYPE h_present(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    HRESULT hr;
    if (hud_enabled) {
        ID3D11Resource *bb = NULL;
        if (!o_omset)
            hook_context(sc);
        if (SUCCEEDED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Resource, (void **)&bb))) {
            if (bb != g_bb) { g_bb = bb; g_last_rtv = NULL; }
            ID3D11Resource_Release(bb); /* the swap chain keeps it alive */
        }
        if (g_bb_draws > g_stat_max) g_stat_max = g_bb_draws;
        g_stat_dropped += g_dropped;
        if (log_stats && ++g_frames % 600 == 0) {
            nc_log("hud: last 600 frames: max %u back-buffer draws per frame, %u dropped", g_stat_max, g_stat_dropped);
            g_stat_max = g_stat_dropped = 0;
        }
    }
    if (g_trace == 2) g_trace = 0;
    if (g_trace == 1) { g_trace = 2; nc_log("trace: next frame"); }
    g_bb_draws = g_dropped = 0;
    g_ui_started = 0;
    g_in_present = 1;
    hr = o_present(sc, sync, flags);
    g_in_present = 0;
    g_bb_draws = 0;
    g_ui_started = 0;
    return hr;
}

static LRESULT CALLBACK dummy_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

__declspec(dllexport) void nc_hud_trace(void) { g_trace = 1; }

int nc_hud_install(void)
{
    hud_enabled = GetPrivateProfileIntA("hud", "hide_while_flying", 1, nc_ini());
    keep_draws = GetPrivateProfileIntA("hud", "keep_draws", 0, nc_ini());
    log_stats = GetPrivateProfileIntA("hud", "log_stats", 0, nc_ini());
    if (!hud_enabled)
        return 0;
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = dummy_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "NoclipDummy";
    RegisterClassA(&wc);
    HWND hw = CreateWindowA("NoclipDummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    DXGI_SWAP_CHAIN_DESC sd;
    memset(&sd, 0, sizeof sd);
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hw;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    IDXGISwapChain *sc = NULL;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
    typedef HRESULT(WINAPI * create_fn)(void *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
                                        const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    create_fn create = d3d11 ? (create_fn)GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain") : NULL;
    if (!create || FAILED(create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx))) {
        nc_log("hud: no D3D11 (game not on DX11?); HUD hiding unavailable");
        DestroyWindow(hw);
        return 0;
    }
    patch(*(void ***)sc, 8, (void *)h_present, (void **)&o_present);
    nc_log("hud: hooked Present (chaining to %p)", o_present);
    IDXGISwapChain_Release(sc);
    ID3D11DeviceContext_Release(ctx);
    ID3D11Device_Release(dev);
    DestroyWindow(hw);
    return 1;
}

void nc_hud_apply(int on)
{
    if (on)
        keep_draws = GetPrivateProfileIntA("hud", "keep_draws", keep_draws, nc_ini());
    g_hide = on && hud_enabled;
}
