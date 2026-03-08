#pragma once
/*
 * canvasos_utils.h — Phase-10: Shell Utility Commands
 * 모든 유틸리티 = PixelCode 셸 내장 명령
 */
#include "canvasos_engine_ctx.h"
#include "canvasos_proc.h"
#include "canvasos_path.h"
#include "canvasos_fd.h"
#include "canvasos_mprotect.h"
#include "canvasos_user.h"
#include "canvas_determinism.h"
#include "canvasos_timewarp.h"
#include "canvasos_detmode.h"

/* 프로세스 */
int  cmd_ps(ProcTable *pt);
int  cmd_kill(ProcTable *pt, uint32_t pid, uint8_t sig);

/* 파일 */
int  cmd_ls(EngineContext *ctx, PathContext *pc, const char *dir);
int  cmd_cd(PathContext *pc, EngineContext *ctx, const char *path);
int  cmd_mkdir(EngineContext *ctx, PathContext *pc, const char *name);
int  cmd_rm(EngineContext *ctx, PathContext *pc, const char *path);
int  cmd_cat(EngineContext *ctx, PathContext *pc, uint32_t pid, const char *path);
int  cmd_echo(uint32_t pid, const char *text);

/* 시스템 */
int  cmd_hash(EngineContext *ctx);
int  cmd_info(EngineContext *ctx, ProcTable *pt);
