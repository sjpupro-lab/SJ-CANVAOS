#ifndef TERVAS_CLI_H
#define TERVAS_CLI_H
#include "tervas_core.h"
#include "canvasos_engine_ctx.h"
int  tv_cli_exec(Tervas *tv, EngineContext *eng, const char *line);
void tv_cli_print_help(void);
#endif
