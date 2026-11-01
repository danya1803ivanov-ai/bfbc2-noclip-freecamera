/* BFBC2Game.exe build 795745 (FileVersion 1.0.1.0, 2011-09-22): addresses used by the Noclip mod.
 * Found with the research tools of the Portal Gun project and re-verified for this mod (see MODLOG.md). */
#pragma once
#include <windows.h>

#define TC __attribute__((thiscall))

/* ServerSoldierEntity: vtable and the per-tick slot this mod chains into (slot 71, 2 stack args) */
#define VT_SERVER_SOLDIER      0x01424850u
#define VT_SLOT_SRV_TICK       0x0142496Cu
#define FN_SRV_TICK_ORIG       0x006B0780u
#define SS_CHARPHYS            0x120

/* CameraManager (static object at 0x1570D80; +0xAC = current camera, +0x60 = view transform the renderer,
 * culling and sound use). Its update (0x7A5D90, main thread, every frame incl. cutscenes) gets the current
 * camera's transform with Camera::getTransform (0x5315E0: thiscall, out LinearTransform*, ret 4) at this call. */
#define CALL_CAM_GET_XFORM     0x007A5E07u
#define FN_CAM_GET_XFORM       0x005315E0u

/* Pause, the same one the in-game menu uses. The UI manager (0x92A160, every UI update) folds its "pause
 * reasons" into a mask at ui+0xC3F24 and keeps Client::setPaused (0x541100: sound system + the game's paused
 * byte) in step with it: mask 0 = run. The main loop then runs every simulation timer at time scale 0 (server,
 * entities, animation, cutscene sequences) while the camera manager and rendering go on. The reason byte at
 * ui+0xC3F2A (mask bit 2) pauses without opening a menu (only bit 1 opens the pause menu). */
#define G_UI                   0x019A4CA8u /* UI manager* */
#define UI_PAUSE_REASON        0xC3F2A
#define G_GAME                 0x0155CCB0u /* Game*; paused byte at +0x9D */
#define GAME_PAUSED            0x9D
#define G_MENU_INPUT           0x01566784u /* non-zero while a menu (pause menu, ...) has the input */

/* The in-process (single-player) server's per-frame update job (cdecl, 1 arg), called at this site. A
 * multiplayer client has no local server, so this never runs there: Noclip uses it as its "single player" gate. */
#define CALL_SERVER_UPDATE     0x0074733Du
#define FN_SERVER_UPDATE       0x0073DD40u

/* character physics (vtable 0x14574A8) */
#define VT_CHARPHYS            0x014574A8u
#define CP_VF_GETVEL           33
#define CP_VF_GETPOS           35
#define CP_VF_SETPOS           68
#define CP_VF_SETVEL           69

/* ClientSoldierEntity */
#define VT_CLIENT_SOLDIER      0x01437950u
#define CS_FLAGS               0x114       /* bit0: the locally controlled soldier */
#define CS_EYE_XFORM           0x2A0       /* LinearTransform rows: right, up, forward, position */
#define CS_EYE_FWD             0x2C0
#define CS_EYE_POS             0x2D0
#define CS_EYE_POS_COPY        0x450       /* copy of the eye transform (+0x420) */
#define CS_FEET                0x6D0       /* soldier transform (+0x6A0) translation */

/* static list: node payload + 0x80 = local ClientSoldierEntity */
#define G_LOCAL_LIST           0x01516900u
#define LOCAL_PAYLOAD_SOLDIER  0x80

/* engine functions */
#define FN_ENTITY_XFORM        0x00610A20u /* thiscall -> LinearTransform* */
#define FN_RAYCAST             0x0057E1D0u /* cdecl (ident, from, to, outPos, outNormal, flags) -> al */
typedef char (*raycast_fn)(const char *, const float *, const float *, float *, float *, int);

/* settings by name: thiscall SettingsManager::find(const char *name, TypeInfo *type) -> value pointer */
#define G_SETTINGS_MGR         0x0155CFDCu
#define FN_SETTING_FIND        0x004F4C40u
#define TI_BOOL                0x01BF0354u
typedef void *(TC *setting_find_fn)(void *mgr, const char *name, void *type);

typedef const float *(TC *vec_get_fn)(void *self);
typedef void (TC *vec_set_fn)(void *self, const float *v);
typedef const float *(TC *xform_fn)(void *self);

static inline void **vtbl(void *o) { return *(void ***)o; }
