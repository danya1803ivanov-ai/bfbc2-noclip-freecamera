/* Noclip for Battlefield: Bad Company 2 (single player, build 795745).
 * The camera itself lives in freecam.c (CameraManager hook). This file holds the soldier's body while the
 * camera flies: every server tick (chained ServerSoldierEntity vtable slot 71, the only safe place to touch
 * Havok) the body is pinned where the flight started (ghost, default) or moved with the camera (ghost=0),
 * and on exit it is put down where the camera is, or back at the start point. */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "game.h"
#include "noclip.h"

/* ---------- config ---------- */
static int exit_to_ground = 1;
static int ghost_mode = 1; /* 1 = ghost: body stays, camera flies (default); 0 = body flies */

static char g_dir[MAX_PATH], g_ini[MAX_PATH];
static FILE *g_log;
nc_state_t nc;

void nc_log(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME t;
    if (!g_log) return;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

const char *nc_ini(void) { return g_ini; }

volatile int nc_input_block;

static void load_config(void)
{
    ghost_mode = GetPrivateProfileIntA("flight", "ghost", ghost_mode, g_ini);
    exit_to_ground = GetPrivateProfileIntA("flight", "exit_to_ground", exit_to_ground, g_ini);
}

/* ---------- vectors ---------- */
static v3 V(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static v3 add(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len(v3 a) { return sqrtf(dot(a, a)); }
static v3 ld(const float *f) { return V(f[0], f[1], f[2]); }

/* ---------- engine wrappers ---------- */
static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    return (const char *)p + n <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

static int ray_hit(v3 a, v3 b, v3 *hit, v3 *nrm)
{
    float f[4] = {a.x, a.y, a.z, 0}, t[4] = {b.x, b.y, b.z, 0}, p[4] = {0}, n[4] = {0};
    if (!((raycast_fn)FN_RAYCAST)("Noclip", f, t, p, n, 2)) return 0;
    if (hit) *hit = ld(p);
    if (nrm) *nrm = ld(n);
    return 1;
}
static int ray(v3 a, v3 b) { return ray_hit(a, b, NULL, NULL); }

static void *charphys(void *ss)
{
    void *cp = *(void **)((char *)ss + SS_CHARPHYS);
    return (cp && (DWORD)vtbl(cp) == VT_CHARPHYS) ? cp : NULL;
}
static v3 phys_pos(void *cp) { return ld(((vec_get_fn)vtbl(cp)[CP_VF_GETPOS])(cp)); }
static void phys_set_pos(void *cp, v3 p) { float f[4] = {p.x, p.y, p.z, 0}; ((vec_set_fn)vtbl(cp)[CP_VF_SETPOS])(cp, f); }
static void phys_set_vel(void *cp, v3 v) { float f[4] = {v.x, v.y, v.z, 0}; ((vec_set_fn)vtbl(cp)[CP_VF_SETVEL])(cp, f); }
static v3 entity_pos(void *e) { return ld(((xform_fn)FN_ENTITY_XFORM)(e) + 12); }

void *nc_local_client_soldier(void)
{
    void **head = (void **)G_LOCAL_LIST, **node = *(void ***)head;
    int guard = 0;
    while (node && node != head && guard++ < 16 && readable(node, 12)) {
        char *payload = (char *)node[2];
        if (readable(payload, LOCAL_PAYLOAD_SOLDIER + 4)) {
            char *cs = *(char **)(payload + LOCAL_PAYLOAD_SOLDIER);
            if (readable(cs, 0x700) && *(DWORD *)cs == VT_CLIENT_SOLDIER && (*(DWORD *)(cs + CS_FLAGS) & 1))
                return cs;
        }
        node = (void **)node[0];
    }
    return NULL;
}

/* room for a standing soldier (feet origin, ~1.8 m tall, ~0.35 m radius)? */
static int body_clear(v3 feet)
{
    v3 c = add(feet, V(0, 0.9f, 0));
    if (ray(add(feet, V(0, 0.05f, 0)), add(feet, V(0, 1.85f, 0)))) return 0;
    if (ray(add(c, V(0.35f, 0, 0)), sub(c, V(0.35f, 0, 0)))) return 0;
    if (ray(add(c, V(0, 0, 0.35f)), sub(c, V(0, 0, 0.35f)))) return 0;
    if (ray(add(c, V(0.25f, 0.6f, 0.25f)), sub(c, V(0.25f, -0.6f, 0.25f)))) return 0;
    return 1;
}

/* ---------- shared state between the main (client) thread and the server thread ---------- */
static void *g_srv, *g_cli;
static volatile int g_req;          /* main thread: 1 = free camera on, hold the body */
static volatile int g_active;       /* server thread: body held (anchor taken) */
static volatile int g_ghost;        /* this session is a ghost flight (body stays at the anchor) */
static volatile int g_return;       /* exit back at the start point instead of at the camera */
static volatile int g_soldier_lost; /* the soldier was missing at some point (cutscene): never teleport it */
static v3 g_anchor, g_eye_off;

void nc_body_request(int on, int back_to_start)
{
    if (on) {
        g_return = 0;
        g_soldier_lost = nc_local_client_soldier() == NULL; /* camera-only start, e.g. inside a cutscene */
    } else {
        g_return = back_to_start || g_soldier_lost;
    }
    g_req = on;
}

void nc_body_follow(v3 cam)
{
    if (g_active)
        nc.pos = sub(cam, g_eye_off); /* feet under the camera */
}

static v3 safe_spot(v3 p, const char **how)
{
    v3 top, n;
    *how = "in place";
    /* buried in terrain: rays that start inside a heightfield see nothing (one-sided), so test from outside:
       a surface above the head and no floor under the feet means we are inside solid ground */
    if (ray_hit(add(p, V(0, 400.f, 0)), add(p, V(0, 1.7f, 0)), &top, &n) && n.y > 0.5f &&
        !ray(add(p, V(0, 1.7f, 0)), sub(p, V(0, 0.5f, 0)))) {
        p = add(top, V(0, 0.05f, 0));
        *how = "lifted out of the ground onto the surface above";
    }
    if (!body_clear(p)) {
        float up;
        for (up = 0.5f; up <= 40.f; up += 0.5f)
            if (body_clear(add(p, V(0, up, 0))) && !ray(add(p, V(0, up + 0.3f, 0)), add(p, V(0, 0.3f, 0)))) {
                *how = "moved up out of geometry";
                return add(p, V(0, up, 0));
            }
        *how = "back to the last open spot";
        return nc.last_safe;
    }
    if (exit_to_ground) {
        /* in the air: land on whatever is below instead of falling (falls of ~15 m+ hurt or kill) */
        v3 g, gn;
        if (!ray(add(p, V(0, 0.3f, 0)), sub(p, V(0, 1.0f, 0))) &&
            ray_hit(add(p, V(0, 0.3f, 0)), sub(p, V(0, 1000.f, 0)), &g, &gn) && gn.y > 0.3f && body_clear(add(g, V(0, 0.05f, 0)))) {
            *how = "landed on the ground below";
            return add(g, V(0, 0.05f, 0));
        }
    }
    return p;
}

/* the body is gone (death, cutscene, level end): let go of it; the camera keeps flying */
static void release_body(const char *why)
{
    if (g_active) {
        g_active = 0;
        nc_extras_apply(0);
        nc_log("body released: %s", why);
    }
    nc.on = 0;
    if (g_req)
        g_soldier_lost = 1;
}

/* ---------- server thread: per-tick slot of ServerSoldierEntity ---------- */
void nc_server_tick(void *ss) __asm__("nc_server_tick");
void nc_server_tick(void *ss)
{
    void *cs, *cp;
    if (*(DWORD *)ss != VT_SERVER_SOLDIER)
        return;
    nc_last_server_tick = GetTickCount();
    cs = nc_local_client_soldier();
    if (!cs) {
        release_body("local soldier gone (death, cutscene or level end)");
        g_cli = g_srv = NULL;
        return;
    }
    if (cs != g_cli) {
        release_body("local soldier changed (respawn, checkpoint or scripted swap)");
        g_cli = cs;
        g_srv = NULL;
    }
    if (!g_srv) {
        /* match the server soldier by position (only while the two agree) */
        if (len(sub(entity_pos(ss), ld((float *)((char *)cs + CS_FEET)))) > 0.5f)
            return;
        g_srv = ss;
        nc_log("local server soldier %p (client %p)", ss, cs);
    }
    if (ss != g_srv)
        return;
    cp = charphys(ss);
    if (!cp)
        return;

    if (g_req && !g_active) {
        g_anchor = phys_pos(cp);
        nc.pos = g_anchor;
        nc.vel = V(0, 0, 0);
        nc.last_safe = g_anchor;
        g_eye_off = sub(ld((float *)((char *)cs + CS_EYE_POS)), ld((float *)((char *)cs + CS_FEET)));
        load_config(); /* Noclip.ini edits apply at the next F6 */
        g_ghost = ghost_mode;
        nc_extras_apply(1);
        nc.on = 1;
        g_active = 1;
        nc_log("holding the body (%s) at (%.2f %.2f %.2f)", g_ghost ? "ghost: body stays" : "body follows the camera",
               g_anchor.x, g_anchor.y, g_anchor.z);
    } else if (!g_req && g_active) {
        const char *how;
        v3 p;
        if (g_return) {
            p = g_anchor;
            how = g_soldier_lost ? "back at the start point (a cutscene happened during the flight)" : "returned to the start point";
        } else {
            p = safe_spot(nc.pos, &how);
        }
        g_active = 0;
        phys_set_pos(cp, p);
        phys_set_vel(cp, V(0, 0, 0));
        nc_extras_apply(0);
        nc.on = 0;
        nc_log("body put down at (%.2f %.2f %.2f), %s", p.x, p.y, p.z, how);
        return;
    }
    if (!g_active)
        return;

    /* ghost: keep the body exactly where it was (game input must not walk it into triggers);
       body mode: move it with the camera. Position only: feeding the flight speed to Havok makes the game
       treat contacts as high-speed impacts (verified: 40 m/s killed the player at once) */
    phys_set_pos(cp, g_ghost ? g_anchor : nc.pos);
    phys_set_vel(cp, V(0, 0, 0));
    nc.ticks++;
    if ((nc.ticks & 15) == 0 && body_clear(nc.pos))
        nc.last_safe = nc.pos;
}

/* ---------- single-player gate: the local server's update job ran recently ---------- */
volatile DWORD nc_last_local_server;
void nc_server_job(void) __asm__("nc_server_job");
void nc_server_job(void) { nc_last_local_server = GetTickCount(); }

int nc_single_player(void)
{
    char *game = *(char **)G_GAME;
    if (game && game[GAME_PAUSED]) /* a paused game stops the server job; only single player can pause */
        return nc_last_local_server != 0;
    return nc_last_local_server && GetTickCount() - nc_last_local_server < 2000;
}

DWORD g_orig_srvjob __asm__("g_orig_srvjob");
__attribute__((naked)) static void srvjob_stub(void)
{
    __asm__("pushal\n\t pushfl\n\t call nc_server_job\n\t popfl\n\t popal\n\t jmp *g_orig_srvjob\n\t");
}

/* ---------- hook plumbing ---------- */
DWORD g_orig_tick __asm__("g_orig_tick");

__attribute__((naked)) static void tick_stub(void)
{
    __asm__("pushal\n\t pushfl\n\t pushl %ecx\n\t call nc_server_tick\n\t addl $4, %esp\n\t popfl\n\t popal\n\t jmp *g_orig_tick\n\t");
}

static int bytes_are(DWORD addr, const char *hex)
{
    const unsigned char *p = (const unsigned char *)addr;
    size_t i, n = strlen(hex) / 2;
    if (!readable(p, n)) return 0;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        if (p[i] != v) return 0;
    }
    return 1;
}

__declspec(dllexport) int nc_is_noclip(void) { return 1; }

static int owned_by_noclip(void *code)
{
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)code, &m))
        return 0;
    return m && GetProcAddress(m, "nc_is_noclip") != NULL;
}

int nc_install(void)
{
    DWORD old, *slot = (DWORD *)VT_SLOT_SRV_TICK;
    if (!bytes_are(FN_SRV_TICK_ORIG, "568bf18b8680020000") || !bytes_are(FN_RAYCAST, "558bec83e4f00f57") ||
        !bytes_are(FN_SETTING_FIND, "85c9740883c1fc") || !bytes_are(FN_CAM_GET_XFORM, "558bec83e4f0d94110")) {
        nc_log("unsupported BFBC2Game.exe build (need 795745); not hooking");
        return 0;
    }
    if (GetModuleHandleA("pbcl.dll")) {
        nc_log("PunkBuster is loaded (multiplayer): Noclip stays off");
        return 0;
    }
    VirtualProtect(slot, 4, PAGE_READWRITE, &old);
    /* chain to whatever is there (another mod), except an older copy of ourselves (dev reload) */
    g_orig_tick = owned_by_noclip((void *)*slot) ? FN_SRV_TICK_ORIG : *slot;
    *slot = (DWORD)tick_stub;
    VirtualProtect(slot, 4, old, &old);
    nc_log("hooked ServerSoldierEntity slot 71 (%p), chaining to %08lX", slot, g_orig_tick);
    {
        unsigned char *call = (unsigned char *)CALL_SERVER_UPDATE;
        DWORD target = (DWORD)(call + 5) + *(DWORD *)(call + 1);
        if (call[0] != 0xE8 || (target != FN_SERVER_UPDATE && !owned_by_noclip((void *)target))) {
            nc_log("server update call at %p not as expected; Noclip stays off", call);
            return 0; /* without the single-player gate the camera must not run at all */
        }
        g_orig_srvjob = FN_SERVER_UPDATE;
        VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old);
        *(DWORD *)(call + 1) = (DWORD)srvjob_stub - (DWORD)(call + 5);
        VirtualProtect(call, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), call, 5);
    }
    return nc_cam_install();
}

void nc_init(HINSTANCE h)
{
    char log[MAX_PATH], *s;
    GetModuleFileNameA(h, g_dir, MAX_PATH);
    s = strrchr(g_dir, '\\');
    if (s) *s = 0;
    snprintf(g_ini, MAX_PATH, "%s\\Noclip.ini", g_dir);
    snprintf(log, MAX_PATH, "%s\\Noclip.log", g_dir);
    g_log = _fsopen(log, "w", 0x40 /* _SH_DENYNO */);
    load_config();
    nc_log("Noclip " NC_VERSION " loaded from %s", g_dir);
}
