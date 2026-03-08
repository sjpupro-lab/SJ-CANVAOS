#pragma once
/* canvasos_shell.h — Patch-E: CanvasShell with Timeline */
#include "canvasos_proc.h"
#include "canvasos_pipe.h"
#include "canvasos_fd.h"
#include "canvasos_path.h"
#include "canvasos_timewarp.h"
#include "canvasos_detmode.h"
#include "canvasos_timeline.h"

#define SHELL_VAR_MAX  32
#define SHELL_VAR_NAME_MAX  16
#define SHELL_VAR_VAL_MAX   64

typedef struct {
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VAL_MAX];
} ShellVar;

typedef struct {
    ProcTable   *pt;
    PipeTable   *pipes;
    PathContext  pathctx;
    TimeWarp     timewarp;
    DetMode      detmode;
    Timeline     timeline;
    ShellVar     vars[SHELL_VAR_MAX];
    int          var_count;
    bool         running;
} Shell;

void shell_init(Shell *sh, ProcTable *pt, PipeTable *pipes, EngineContext *ctx);
int  shell_exec_line(Shell *sh, EngineContext *ctx, const char *line);
int  shell_exec_pipe(Shell *sh, EngineContext *ctx, const char *line); /* a | b */
int  shell_exec_redir(Shell *sh, EngineContext *ctx, const char *line); /* > < >> */
void shell_set_var(Shell *sh, const char *name, const char *value);
const char *shell_get_var(const Shell *sh, const char *name);
