#include "../include/canvasos_engine_ctx.h"
/*
 * canvas_delta.c — GPU Delta Export 구현
 */
#include "../include/canvas_delta.h"
#include <stdlib.h>
#include <string.h>

/*
 * gpu_delta_export_build
 *
 * 모든 lane의 Δ를 하나의 연속 배열로 flatten.
 * GPU SSBO로 직접 업로드 가능한 포맷.
 *
 * 레이아웃:
 *   [lane0 deltas] [lane1 deltas] ... [lane255 deltas]
 *   → lanes[i].offset, lanes[i].count 로 slice 접근
 *
 * GPU merge shader 의사코드:
 *   for each DeltaCell dc in flat:
 *     uvec2 coord = uvec2(dc.addr % CANVAS_W, dc.addr / CANVAS_W)
 *     imageStore(canvas, ivec2(coord), unpack_cell(dc))
 *   (정렬 보장: lane_id 오름차순 → LAST_WINS 자동 적용)
 */
int gpu_delta_export_build(const LaneDelta *ld_array, int n_lanes,
                            GpuDeltaExport *out)
{
    if (!ld_array || !out || n_lanes <= 0) return -1;
    memset(out, 0, sizeof(*out));

    /* 총 DeltaCell 수 계산 */
    uint32_t total = 0;
    for (int i = 0; i < n_lanes && i < MAX_LANES; i++)
        total += ld_array[i].count;

    if (total == 0) return 0;

    out->flat = (DeltaCell *)malloc(total * sizeof(DeltaCell));
    if (!out->flat) return -1;
    out->total = total;

    uint32_t offset = 0;
    uint32_t lc     = 0;

    for (int i = 0; i < n_lanes && i < MAX_LANES; i++) {
        const LaneDelta *d = &ld_array[i];
        if (d->count == 0) continue;

        memcpy(out->flat + offset, d->cells,
               d->count * sizeof(DeltaCell));

        out->lanes[lc++] = (GpuDeltaLaneInfo){
            .lane_id = (uint32_t)i,
            .offset  = offset,
            .count   = d->count,
            .tick    = d->tick,
        };
        offset += d->count;
    }
    out->lane_count = lc;
    return 0;
}

void gpu_delta_export_free(GpuDeltaExport *ex)
{
    if (!ex) return;
    free(ex->flat);
    memset(ex, 0, sizeof(*ex));
}
