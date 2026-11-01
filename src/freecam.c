/* Free camera: one transform for everything the game renders, gameplay and cutscenes alike.
 * CameraManager::update (0x7A5D90, the static manager at 0x1570D80) asks the current camera for its
 * transform with 0x5315E0 and copies the result into the view the renderer, culling and sound use. The call
 * at 0x7A5E07 is redirected here: the game's camera still runs, and while the free camera is on its result
 * is replaced. Main thread, once per frame, also while a cutscene owns the camera. */
#include <windows.h>
#include <math.h>
#include <string.h>
#include "game.h"
#include "noclip.h"

static int key_toggle = VK_F6, key_fwd = 'W', key_back = 'S', key_left = 'A', key_right = 'D';
static int key_up = VK_SPACE, key_down = VK_CONTROL, key_fast = VK_SHIFT, key_slow = VK_MENU, key_return = VK_BACK;
static int key_pause = VK_F8;
volatile int nc_paused_by_us;
static float speed = 8.f, fast_mult = 5.f, slow_mult = 0.25f, smooth_time = 0.06f, sens = 0.12f;
static int fly_along_view = 1, invert_y = 0;

static volatile int g_on;           /* free camera active */
static float g_yaw, g_pitch, g_hand, g_yaw_sign;
static v3 g_cam, g_vel;
static LARGE_INTEGER g_freq, g_last;

static v3 V(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static v3 add(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static v3 mul(v3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 cross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static v3 norm(v3 a) { float l = sqrtf(dot(a, a)); return l > 1e-6f ? mul(a, 1.f / l) : a; }
static v3 ld(const float *f) { return V(f[0], f[1], f[2]); }
static void st(float *f, v3 v) { f[0] = v.x; f[1] = v.y; f[2] = v.z; }
static int down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

static int game_focused(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static float ini_float(const char *sec, const char *key, float def)
{
    char buf[64];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof buf, nc_ini());
    return buf[0] ? (float)atof(buf) : def;
}

void nc_cam_config(void)
{
    const char *ini = nc_ini();
    key_toggle = GetPrivateProfileIntA("keys", "toggle", key_toggle, ini);
    key_fwd = GetPrivateProfileIntA("keys", "forward", key_fwd, ini);
    key_back = GetPrivateProfileIntA("keys", "back", key_back, ini);
    key_left = GetPrivateProfileIntA("keys", "left", key_left, ini);
    key_right = GetPrivateProfileIntA("keys", "right", key_right, ini);
    key_up = GetPrivateProfileIntA("keys", "up", key_up, ini);
    key_down = GetPrivateProfileIntA("keys", "down", key_down, ini);
    key_fast = GetPrivateProfileIntA("keys", "fast", key_fast, ini);
    key_slow = GetPrivateProfileIntA("keys", "slow", key_slow, ini);
    key_return = GetPrivateProfileIntA("keys", "return_to_start", key_return, ini);
    key_pause = GetPrivateProfileIntA("keys", "pause", key_pause, ini);
    speed = ini_float("flight", "speed", speed);
    fast_mult = ini_float("flight", "fast_multiplier", fast_mult);
    slow_mult = ini_float("flight", "slow_multiplier", slow_mult);
    smooth_time = ini_float("flight", "smoothing_seconds", smooth_time);
    fly_along_view = GetPrivateProfileIntA("flight", "fly_along_view", fly_along_view, ini);
    sens = ini_float("camera", "mouse_degrees_per_count", sens);
    invert_y = GetPrivateProfileIntA("camera", "invert_y", invert_y, ini);
}

int nc_flight_keys(int *out, int max)
{
    int keys[9], i, n = 0;
    keys[0] = key_fwd; keys[1] = key_back; keys[2] = key_left; keys[3] = key_right; keys[4] = key_up;
    keys[5] = key_down; keys[6] = key_fast; keys[7] = key_slow; keys[8] = key_return;
    for (i = 0; i < 9 && n < max; i++)
        if (keys[i]) out[n++] = keys[i];
    return n;
}

static v3 fwd_of(float yaw, float pitch)
{
    return V(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
}

/* take over from whatever camera the game shows right now (rows: side, up, forward, position) */
static void start(const float *m)
{
    v3 r0 = ld(m), r1 = ld(m + 4), f = norm(ld(m + 8));
    g_cam = ld(m + 12);
    g_vel = V(0, 0, 0);
    g_yaw = atan2f(f.x, f.z);
    g_pitch = asinf(f.y < -1.f ? -1.f : f.y > 1.f ? 1.f : f.y);
    /* yaw/pitch describe row 2; this transform looks along -row 2 with row 0 = the viewer's right (unlike the
       soldier's eye transform). Keep the game's handedness when building our own rows. */
    g_hand = dot(r0, cross(r1, f)) < 0.f ? -1.f : 1.f;
    /* mouse right: row 2 turns towards -row 0, so the view (-row 2) turns towards +row 0, the right */
    g_yaw_sign = dot(V(cosf(g_yaw), 0, -sinf(g_yaw)), mul(r0, -1.f)) >= 0.f ? 1.f : -1.f;
    QueryPerformanceCounter(&g_last);
}

static void build(float *m)
{
    v3 f = fwd_of(g_yaw, g_pitch), up0 = V(0, 1, 0);
    v3 s = mul(norm(cross(up0, f)), g_hand);
    v3 u = norm(cross(f, s));
    if (dot(u, up0) < 0.f) u = mul(u, -1.f);
    st(m, s); m[3] = 0;
    st(m + 4, u); m[7] = 0;
    st(m + 8, f); m[11] = 0;
    st(m + 12, g_cam); m[15] = 1;
}

static float frame_dt(void)
{
    LARGE_INTEGER now;
    float dt;
    QueryPerformanceCounter(&now);
    dt = (float)(now.QuadPart - g_last.QuadPart) / (float)g_freq.QuadPart;
    g_last = now;
    return dt < 0.f ? 0.f : dt > 0.1f ? 0.1f : dt; /* pause / hitch: never jump */
}

static void fly(float dt, int focused)
{
    int mx = 0, my = 0;
    v3 f, right, wish = V(0, 0, 0);
    nc_mouse_take(&mx, &my);
    g_yaw += g_yaw_sign * (float)mx * sens * 0.0174533f;
    /* the manager's transform looks along -row 2 (verified in game: +row 2 pitched the view the wrong way) */
    g_pitch += (invert_y ? -1.f : 1.f) * (float)my * sens * 0.0174533f;
    if (g_pitch > 1.55f) g_pitch = 1.55f;
    if (g_pitch < -1.55f) g_pitch = -1.55f;
    f = mul(fwd_of(g_yaw, g_pitch), -1.f);                           /* where the camera looks */
    right = mul(V(cosf(g_yaw), 0, -sinf(g_yaw)), -g_yaw_sign);       /* the viewer's right (+row 0) */
    if (!fly_along_view) f = norm(V(f.x, 0, f.z));
    if (focused) {
        if (down(key_fwd)) wish = add(wish, f);
        if (down(key_back)) wish = sub(wish, f);
        if (down(key_right)) wish = add(wish, right);
        if (down(key_left)) wish = sub(wish, right);
        if (down(key_up)) wish = add(wish, V(0, 1, 0));
        if (down(key_down)) wish = sub(wish, V(0, 1, 0));
    }
    if (dot(wish, wish) > 1.f) wish = norm(wish); /* diagonals are not faster */
    {
        float s = speed * (focused && down(key_fast) ? fast_mult : 1.f) * (focused && down(key_slow) ? slow_mult : 1.f);
        float k = smooth_time > 0.f ? dt / smooth_time : 1.f;
        if (k > 1.f) k = 1.f;
        g_vel = add(g_vel, mul(sub(mul(wish, s), g_vel), k)); /* critically damped: no overshoot */
        g_cam = add(g_cam, mul(g_vel, dt));
    }
}

/* the in-game menu's own pause, without the menu: simulation, cutscene sequences and sound stop; rendering and
   this camera go on. The UI applies the reason on its next update. */
static void toggle_pause(void)
{
    char *ui = *(char **)G_UI;
    if (!ui)
        return;
    if (!ui[UI_PAUSE_REASON] && !nc_single_player()) {
        nc_log("F8 ignored: single player only (no local server running)");
        return;
    }
    ui[UI_PAUSE_REASON] = !ui[UI_PAUSE_REASON];
    nc_paused_by_us = ui[UI_PAUSE_REASON];
    nc_log("%s", nc_paused_by_us ? "paused (F8)" : "resumed (F8)");
}

/* after the game copied the current camera's transform into `m` (16 floats) */
void nc_cam_post(float *m) __asm__("nc_cam_post");
void nc_cam_post(float *m)
{
    static int prev_toggle, prev_return, prev_pause;
    /* the pause menu uses the same pause reason as F8 and needs the mouse: no keys of ours while it is open */
    int menu = *(volatile DWORD *)G_MENU_INPUT != 0;
    int focused = game_focused() && !menu;
    int t = focused && down(key_toggle), r = focused && down(key_return), pz = focused && down(key_pause);
    if (pz && !prev_pause)
        toggle_pause();
    prev_pause = pz;
    if (nc_paused_by_us && *(char **)G_UI && !(*(char **)G_UI)[UI_PAUSE_REASON])
        nc_paused_by_us = 0; /* cleared elsewhere (level change) */
    if (t && !prev_toggle) {
        if (!g_on && !nc_single_player()) {
            nc_log("F6 ignored: single player only (no local server running)");
        } else if (!g_on) {
            nc_cam_config();
            start(m);
            nc_body_request(1, 0);
            nc_input_on_toggle(1);
            nc_hud_apply(1);
            g_on = 1;
            nc_log("free camera ON at (%.2f %.2f %.2f)", g_cam.x, g_cam.y, g_cam.z);
        } else {
            g_on = 0;
            nc_body_request(0, 0);
            nc_hud_apply(0);
            nc_log("free camera OFF at (%.2f %.2f %.2f)", g_cam.x, g_cam.y, g_cam.z);
        }
    } else if (r && !prev_return && g_on) {
        g_on = 0;
        nc_body_request(0, 1);
        nc_hud_apply(0);
        nc_log("free camera OFF, soldier back to the start point");
    }
    prev_toggle = t;
    prev_return = r;
    nc_input_block = g_on && !menu;
    nc_mouse_capture = g_on && !menu;
    if (!g_on)
        return;
    if (menu) {
        QueryPerformanceCounter(&g_last); /* hold still behind the menu, no jump afterwards */
        build(m);
        return;
    }
    fly(frame_dt(), focused);
    nc_body_follow(g_cam);
    build(m);
}

DWORD g_cam_orig __asm__("g_cam_orig");

/* replaces `call 0x5315E0` (thiscall: ecx = camera, [esp+4] = out, ret 4) */
__attribute__((naked)) static void cam_stub(void)
{
    __asm__("pushl 4(%esp)\n\t call *g_cam_orig\n\t"
            "pushal\n\t pushfl\n\t pushl 40(%esp)\n\t call nc_cam_post\n\t addl $4, %esp\n\t popfl\n\t popal\n\t"
            "ret $4\n\t");
}

static int owned_by_noclip(void *code)
{
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)code, &m))
        return 0;
    return m && GetProcAddress(m, "nc_is_noclip") != NULL;
}

int nc_cam_install(void)
{
    unsigned char *call = (unsigned char *)CALL_CAM_GET_XFORM;
    DWORD old, target;
    QueryPerformanceFrequency(&g_freq);
    nc_cam_config();
    if (call[0] != 0xE8) {
        nc_log("free camera: unexpected code at %p; not hooking", call);
        return 0;
    }
    target = (DWORD)(call + 5) + *(DWORD *)(call + 1);
    if (target != FN_CAM_GET_XFORM && !owned_by_noclip((void *)target)) {
        nc_log("free camera: call at %p goes to %08lX (another mod?); not hooking", call, target);
        return 0;
    }
    g_cam_orig = FN_CAM_GET_XFORM;
    VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD *)(call + 1) = (DWORD)cam_stub - (DWORD)(call + 5);
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    nc_log("free camera: hooked CameraManager::update transform at %p", call);
    return 1;
}
