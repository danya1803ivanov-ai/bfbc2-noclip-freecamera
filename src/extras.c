/* Things switched on while flying and switched back afterwards: engine bool settings looked up by name
 * through the game's own settings registry (e.g. "Server.IsGodMode"). Values are saved on enable and
 * written back on disable. Only names listed in Noclip.ini [while_noclip] are touched. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "noclip.h"

#define MAXSET 16
static struct { char name[64]; unsigned char *ptr; unsigned char want, saved; } g_set[MAXSET];
static int g_nset, g_loaded;

static void load(void)
{
    char buf[2048], *p;
    void *mgr;
    g_loaded = 1;
    mgr = *(void **)G_SETTINGS_MGR;
    if (!mgr) return;
    GetPrivateProfileSectionA("while_noclip", buf, sizeof buf, nc_ini());
    for (p = buf; *p && g_nset < MAXSET; p += strlen(p) + 1) {
        char *eq = strchr(p, '=');
        if (*p == ';' || !eq) continue;
        *eq = 0;
        strncpy(g_set[g_nset].name, p, sizeof g_set[0].name - 1);
        g_set[g_nset].want = (unsigned char)atoi(eq + 1);
        g_set[g_nset].ptr = ((setting_find_fn)FN_SETTING_FIND)(mgr, g_set[g_nset].name, (void *)TI_BOOL);
        nc_log("setting %s -> %p (%s)", g_set[g_nset].name, g_set[g_nset].ptr, g_set[g_nset].ptr ? "found" : "unknown, ignored");
        if (g_set[g_nset].ptr) g_nset++;
        *eq = '=';
    }
}

void nc_extras_apply(int on)
{
    int i;
    if (!g_loaded) load();
    for (i = 0; i < g_nset; i++) {
        if (on) {
            g_set[i].saved = *g_set[i].ptr;
            *g_set[i].ptr = g_set[i].want;
        } else {
            *g_set[i].ptr = g_set[i].saved;
        }
    }
}
