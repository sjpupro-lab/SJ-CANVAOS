/*
 * tervas_main.c — Tervas standalone entry point
 *
 * Initializes engine + tervas, runs interactive CLI loop.
 */
#include "../../include/tervas_core.h"
#include "../../include/tervas_bridge.h"
#include "../../include/tervas_cli.h"
#include "../../include/tervas_render.h"
#include "../../include/canvasos_engine_ctx.h"
#include "../../include/canvasos_gate_ops.h"
#include <stdio.h>
#include <string.h>

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];

int main(void) {
    printf("Tervas — CanvasOS Canvas Terminal (Phase-7)\n");
    printf("Type 'help' for commands, 'quit' to exit.\n\n");

    EngineContext eng;
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_gates, 0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    engctx_init(&eng, g_cells, CANVAS_W * CANVAS_H, g_gates, g_active, NULL);
    gate_open_tile(&eng, 10);
    engctx_tick(&eng);

    Tervas tv;
    tervas_init(&tv);
    tervas_bridge_attach(&tv, &eng);
    tervas_bridge_snapshot(&tv, &eng, eng.tick);
    tv.running = true;

    char line[512];
    while (tv.running) {
        printf("tervas> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        /* strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        int rc = tv_cli_exec(&tv, &eng, line);
        if (rc != TV_OK && rc != TV_ERR_CMD)
            printf("  error: %d\n", rc);
        if (rc == TV_ERR_CMD)
            printf("  unknown command. type 'help'\n");
    }

    tervas_free(&tv);
    printf("bye.\n");
    return 0;
}
