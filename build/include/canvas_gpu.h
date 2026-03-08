#pragma once
/*
 * canvas_gpu.h — Phase 6: GPU 백엔드 인터페이스
 *
 * ===================================================================
 * 설계 원칙 (DK-2 준수)
 * ===================================================================
 *
 * - GPU 내부는 rgba8ui / rgba16ui 정수 텍셀만 사용 (float 금지)
 * - 리덕션/merge 순서는 cell_index 오름차순 고정 (DK-3)
 * - Phase 6에서 기본은 CPU fallback (canvas_gpu_stub.c)
 * - Phase 7+에서 실제 OpenCL/Vulkan/CUDA로 교체 예정
 *
 * 빌드 플래그:
 *   -DSJ_GPU=0   CPU fallback (기본)
 *   -DSJ_GPU=1   GPU 경로 활성화 (Phase 7+)
 *
 * ===================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvas_merge.h"
#include "engine_time.h"
#include "canvas_bh_compress.h"

#ifndef SJ_GPU
#define SJ_GPU 0
#endif

/* ── GPU 능력 플래그 ── */
typedef struct {
    bool     available;       /* GPU 사용 가능 여부 */
    bool     integer_texel;   /* rgba8ui/rgba16ui 지원 */
    uint32_t max_tiles;       /* 한 번에 처리 가능한 타일 수 */
    char     backend[32];     /* "stub" / "opencl" / "vulkan" */
} GpuCaps;

/* ── GPU 컨텍스트 (불투명 핸들) ── */
typedef struct GpuCtx GpuCtx;

/* ── API ── */

/* 초기화 + 능력 조회 */
GpuCtx *gpu_init(void);
void    gpu_destroy(GpuCtx *g);
GpuCaps gpu_get_caps(const GpuCtx *g);

/*
 * gpu_upload_tiles: 캔버스 타일 데이터를 GPU로 전송.
 * open_tiles: 처리할 타일 ID 배열, n: 개수.
 * CPU fallback 시 no-op (데이터는 이미 메모리에 있음).
 */
int gpu_upload_tiles(GpuCtx *g, const EngineContext *ctx,
                     const uint16_t *open_tiles, uint32_t n);

/*
 * gpu_scan_active_set: 열린 타일만 스캔 + opcode 실행.
 * 결과: delta_out에 변경된 셀 기록.
 * [DK-2] 정수 연산만. [DK-3] tile_id 오름차순.
 */
int gpu_scan_active_set(GpuCtx *g, EngineContext *ctx,
                        const uint16_t *open_tiles, uint32_t n,
                        Delta *delta_out, uint32_t *delta_count);

/*
 * gpu_bh_summarize_idle: BH-IDLE 압축을 GPU로 수행.
 * [DK-1] 틱 경계에서만 호출.
 */
int gpu_bh_summarize_idle(GpuCtx *g, EngineContext *ctx,
                          uint32_t from_tick, uint32_t to_tick,
                          uint16_t gate_id);

/*
 * gpu_merge_delta_tiles: Δ 배열을 GPU에서 캔버스에 적용.
 * [DK-3] cell_index 오름차순 보장.
 */
int gpu_merge_delta_tiles(GpuCtx *g, EngineContext *ctx,
                          const Delta *deltas, uint32_t count);
