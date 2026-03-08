#pragma once
/*
 * canvasos_pixel_loader.h — Patch-D: PixelCode Self-Hosting Layer
 *
 * Provides:
 *   - Utility registry: name → PixelCode program ID
 *   - Program planter: generates VM cells on canvas for each utility
 *   - Execution mode: PXL_MODE_PIXELCODE vs PXL_MODE_C_FALLBACK
 *   - Shell integration: dispatch utility via PixelCode VM
 */
#include "canvasos_vm.h"
#include "canvasos_engine_ctx.h"
#include "canvasos_proc.h"
#include "canvasos_pipe.h"

/* ── Utility IDs ─────────────────────────────────────── */
#define PXL_UTIL_NONE   0
#define PXL_UTIL_ECHO   1
#define PXL_UTIL_CAT    2
#define PXL_UTIL_INFO   3
#define PXL_UTIL_HASH   4
#define PXL_UTIL_HELP   5

/* ── Execution Mode ──────────────────────────────────── */
#define PXL_MODE_C_FALLBACK  0   /* Use C functions (legacy) */
#define PXL_MODE_PIXELCODE   1   /* Use PixelCode programs */

/* ── Canvas region for utility programs ──────────────── */
#define PXL_PROG_X  900u    /* Programs planted at x=900 */
#define PXL_PROG_Y  0u      /* Starting y=0 */

/* ── API ─────────────────────────────────────────────── */

/* Registry */
int  pxl_find_utility(const char *name);

/* Mode */
void pxl_set_mode(int mode);
int  pxl_get_mode(void);

/* Plant programs on canvas (returns cell count planted) */
int  pxl_plant_echo(EngineContext *ctx, uint32_t x, uint32_t y, const char *arg);
int  pxl_plant_cat(EngineContext *ctx, uint32_t x, uint32_t y,
                   const char *path, ProcTable *pt);
int  pxl_plant_info(EngineContext *ctx, uint32_t x, uint32_t y, ProcTable *pt);
int  pxl_plant_hash(EngineContext *ctx, uint32_t x, uint32_t y);
int  pxl_plant_help(EngineContext *ctx, uint32_t x, uint32_t y);

/* Execute a utility via PixelCode (plant + run VM) */
int  pxl_exec_utility(EngineContext *ctx, ProcTable *pt, PipeTable *pipes,
                      const char *cmd, const char *arg);
