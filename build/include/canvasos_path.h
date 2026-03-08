#pragma once
/* canvasos_path.h — Phase-8: Path Resolution */
#include <stdint.h>
#include "canvasfs.h"
#include "canvasos_engine_ctx.h"

typedef struct {
    uint32_t  pid;
    FsKey     cwd;       /* current working directory */
    FsKey     root;      /* root directory */
} PathContext;

void pathctx_init(PathContext *pc, uint32_t pid, FsKey root);
int  path_resolve(EngineContext *ctx, PathContext *pc,
                  const char *path, FsKey *out);
int  path_mkdir(EngineContext *ctx, PathContext *pc, const char *name);
int  path_ls(EngineContext *ctx, PathContext *pc, FsKey dir,
             char names[][16], FsKey keys[], int max);
int  path_cd(PathContext *pc, EngineContext *ctx, const char *path);
int  path_rm(EngineContext *ctx, PathContext *pc, const char *path);
