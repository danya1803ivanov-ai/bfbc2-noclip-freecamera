#pragma once
#include <windows.h>

#define NC_VERSION "2.0.0"

typedef struct v3 { float x, y, z; } v3;

typedef struct nc_state {
    int on;
    v3 pos, vel, last_safe;
    unsigned ticks;
} nc_state_t;

extern nc_state_t nc;
void nc_log(const char *fmt, ...);
const char *nc_ini(void);
void *nc_local_client_soldier(void);
int nc_install(void);
void nc_init(HINSTANCE h);
void nc_extras_apply(int on); /* engine settings while the body is held; restores everything on 0 */
void nc_hud_apply(int on);
int nc_hud_install(void);
extern DWORD nc_last_server_tick; /* GetTickCount of the last server tick (pause detection) */

/* noclip.c (server thread): the soldier's body while the free camera flies */
void nc_body_request(int on, int back_to_start); /* main thread: free camera switched on/off */
void nc_body_follow(v3 cam);                     /* main thread, every frame: where the camera (eye) is */
int nc_single_player(void);                      /* a local (single-player) server is running */

/* freecam.c: the camera itself (CameraManager hook) */
int nc_cam_install(void);
void nc_cam_config(void);
int nc_flight_keys(int *out, int max);
extern volatile int nc_paused_by_us; /* F8 pause is on (no menu): the HUD filter keeps hiding the UI */

/* input.c: while set, the game does not see the flight keys or mouse buttons (DirectInput filter) */
extern volatile int nc_input_block;
extern volatile int nc_mouse_capture;  /* mouse movement goes to the free camera instead of the game */
void nc_mouse_take(int *dx, int *dy);
int nc_input_install(void);
void nc_input_on_toggle(int on);
