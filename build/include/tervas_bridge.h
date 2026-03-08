#ifndef TERVAS_BRIDGE_H
#define TERVAS_BRIDGE_H
#include <stddef.h>
#include "tervas_core.h"
#include "canvasos_engine_ctx.h"
#include "engine_time.h"

int tervas_bridge_attach  (Tervas *tv, EngineContext *eng);
int tervas_bridge_snapshot(Tervas *tv, EngineContext *eng, uint32_t tick);
int tervas_bridge_inspect (EngineContext *eng, uint32_t x, uint32_t y,
                            char *buf, size_t buflen);
int tervas_bridge_region  (Tervas *tv, EngineContext *eng, const char *name);
#endif
