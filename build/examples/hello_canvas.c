/*
 * hello_canvas.c — CanvasOS 예시 구현
 *
 * "HELLO"를 캔버스 중심에 수직 적층(Y축 = 시간축)하고,
 * 게이트 제어 → WH 기록 → 리플레이 → CVP 저장/로드 를 시연한다.
 *
 * 이 코드 하나로 CanvasOS의 핵심 개념을 모두 확인할 수 있다:
 *   1. Cell의 ABGR 채널 계약
 *   2. 수직 적층 (Y축 = 시간, 아래로 쌓임)
 *   3. 게이트 OPEN/CLOSE (타일 단위 실행 제어)
 *   4. WH 레코드 (시간 기록 + 리플레이)
 *   5. BH 에너지 (자동 감쇠)
 *   6. CVP 저장/로드 (결정론 스냅샷)
 *   7. Tervas inspect (읽기 전용 조회)
 *
 * 빌드:
 *   make hello_canvas
 *
 * 실행:
 *   ./hello_canvas          (Linux/macOS)
 *   cp hello_canvas ~ && ~/hello_canvas   (Android Termux)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvas_bh_compress.h"
#include "../include/cvp_io.h"
#include "../include/tervas/tervas_core.h"
#include "../include/tervas/tervas_bridge.h"
#include "../include/tervas/tervas_projection.h"
#include "../include/tervas/tervas_render_cell.h"

/* ── 전역 캔버스 메모리 (8 MB) ─────────────────────────────── */
static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];

/* ── 헬퍼 ────────────────────────────────────────────────── */
static void print_sep(void) {
    printf("────────────────────────────────────────────\n");
}
static void print_cell(const char *label, const EngineContext *ctx,
                       uint32_t x, uint32_t y) {
    const Cell *c = &ctx->cells[y * CANVAS_W + x];
    uint32_t tid = tile_id_of_xy((uint16_t)x, (uint16_t)y);
    printf("  %s (%u,%u)  A=%08X B=%02X G=%03u R=%02X('%c')  "
           "tile=%u gate=%s\n",
           label, x, y, c->A, c->B, c->G, c->R,
           (c->R >= 0x20 && c->R < 0x7F) ? c->R : '.',
           tid, gate_is_open_tile(ctx, (uint16_t)tid) ? "OPEN" : "CLOSE");
}

/* ═══════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════ */
int main(void) {

    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   CanvasOS  예시: HELLO 수직 적층        ║\n");
    printf("║   v1.0.1-p7  ·  Phase-7 Tervas 통합     ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* ──────────────────────────────────────────────────────
     * STEP 1: 엔진 초기화
     *
     * 8MB 캔버스를 0으로 초기화하고 EngineContext를 설정한다.
     * 이 시점에서 모든 게이트는 CLOSE, 모든 셀은 비어있다.
     * ────────────────────────────────────────────────────── */
    printf("[STEP 1] 엔진 초기화\n");

    memset(g_cells,  0, sizeof(g_cells));
    memset(g_gates,  0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));

    EngineContext ctx;
    engctx_init(&ctx, g_cells, CANVAS_W * CANVAS_H,
                g_gates, g_active, NULL);

    printf("  캔버스: %u × %u = %u 셀 (%zu MB)\n",
           CANVAS_W, CANVAS_H, CANVAS_W * CANVAS_H,
           sizeof(g_cells) / (1024 * 1024));
    printf("  게이트: %u 타일 (전부 CLOSE)\n", TILE_COUNT);
    printf("  tick: %u\n", ctx.tick);
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 2: 게이트 열기
     *
     * 중심 (512,512)가 속한 타일의 게이트를 연다.
     * 게이트가 CLOSE면 해당 타일의 셀은 실행 대상에서 제외된다.
     *
     * tile_id = (512/16)*64 + (512/16) = 32*64 + 32 = 2080
     * ────────────────────────────────────────────────────── */
    printf("[STEP 2] 게이트 OPEN — 중심 타일\n");

    uint32_t center_tile = tile_id_of_xy(512, 512);
    gate_open_tile(&ctx, (uint16_t)center_tile);

    printf("  tile_id=%u  gate=%s\n",
           center_tile,
           gate_is_open_tile(&ctx, (uint16_t)center_tile) ? "OPEN" : "CLOSE");
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 3: "HELLO"를 수직 적층
     *
     * CanvasOS의 핵심 개념: Y축 = 시간축
     * 데이터는 가로로 나열하지 않고, 아래로 쌓는다.
     * 같은 X(512)에서 Y를 512→516으로 증가시키며 기록한다.
     *
     * 각 셀에 ABGR 채널 계약에 따라 기록:
     *   A = 공간 주소 (여기서는 Lane ID 인코딩)
     *   B = 동작 (opcode, 여기서는 0x01=PRINT)
     *   G = 에너지 (100 = 활성)
     *   R = 데이터 (ASCII 문자)
     *
     * 이것이 "8MB 안에 데이터를 압축 적층하는" 핵심이다.
     * 나란히 배열하면 1차원 배열이지만,
     * 수직 적층하면 (x, y, tick) 3차원 공간이 된다.
     * ────────────────────────────────────────────────────── */
    printf("[STEP 3] \"HELLO\" 수직 적층 (x=512, y=512~516)\n");
    printf("         Y축 = 시간축, 아래로 쌓는다\n\n");

    const char *hello = "HELLO";
    uint32_t x0 = 512, y0 = 512;

    for (int i = 0; hello[i]; i++) {
        uint32_t x = x0;
        uint32_t y = y0 + (uint32_t)i;    /* Y↓ = 미래 방향 */
        uint32_t idx = y * CANVAS_W + x;

        /* ABGR 채널 계약 준수 */
        ctx.cells[idx].A = 0x00010000u | (uint32_t)i;  /* Lane 1 + 순번 */
        ctx.cells[idx].B = 0x01;                        /* opcode: PRINT */
        ctx.cells[idx].G = 100;                         /* energy: 100   */
        ctx.cells[idx].R = (uint8_t)hello[i];           /* payload: char */

        /* tick 진행 + WH 기록 */
        engctx_tick(&ctx);

        print_cell(">", &ctx, x, y);
    }

    printf("\n  적층 완료: 5개 셀, 5 ticks\n");
    printf("  같은 X좌표에 시간순으로 데이터가 쌓여 있다\n");
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 4: BH 에너지 설정
     *
     * pid=1에 에너지 200을 부여한다.
     * 매 tick마다 에너지가 감소하고, 0이 되면 자동 CLOSE된다.
     * ────────────────────────────────────────────────────── */
    printf("[STEP 4] BH 에너지 설정 (pid=1, energy=200)\n");

    bh_set_energy(&ctx, 1, 200, 255);
    uint8_t e = bh_get_energy(&ctx, 1);
    printf("  pid=1 에너지: %u\n", e);

    /* 에너지 감쇠 시연 */
    uint8_t e2 = bh_decay_energy(&ctx, 1, 50);
    printf("  감쇠 후 (-50): %u\n", e2);
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 5: 결정론 해시 확인
     *
     * DK-3: cell_index 오름차순으로 전체 캔버스를 해싱한다.
     * 같은 입력이면 항상 같은 해시가 나와야 한다.
     * ────────────────────────────────────────────────────── */
    printf("[STEP 5] 결정론 해시 (DK-3)\n");

    uint32_t hash1 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    uint32_t hash2 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    printf("  hash1 = 0x%08X\n", hash1);
    printf("  hash2 = 0x%08X\n", hash2);
    printf("  동일 = %s\n", hash1 == hash2 ? "YES (결정론 통과)" : "NO (위반!)");
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 6: CVP 저장
     *
     * 현재 캔버스 상태를 .cvp 파일로 저장한다.
     * 헤더에 결정론 락 필드가 포함된다:
     *   scan_policy, bpage_version, contract_hash, wh_cap
     * ────────────────────────────────────────────────────── */
    printf("[STEP 6] CVP 저장\n");

    CvpStatus st = cvp_save_ctx(&ctx, "hello.cvp",
                                 SCAN_RING_MH, 0,
                                 CVP_CONTRACT_HASH_V1, 0);
    printf("  저장: %s  (%s)\n", "hello.cvp", cvp_strerror(st));
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 7: CVP 로드 + 해시 비교
     *
     * 저장한 파일을 다른 컨텍스트에 로드하고,
     * 해시가 동일한지 확인한다.
     * ────────────────────────────────────────────────────── */
    printf("[STEP 7] CVP 로드 + 해시 비교\n");

    /* 새 캔버스에 로드 */
    static Cell      g2_cells[CANVAS_W * CANVAS_H];
    static GateState g2_gates[TILE_COUNT];
    static uint8_t   g2_active[TILE_COUNT];
    EngineContext ctx2;
    engctx_init(&ctx2, g2_cells, CANVAS_W * CANVAS_H,
                g2_gates, g2_active, NULL);

    CvpStatus st2 = cvp_load_ctx(&ctx2, "hello.cvp", false,
                                  SCAN_RING_MH, CVP_LOCK_SKIP,
                                  CVP_CONTRACT_HASH_V1);
    uint32_t hash_loaded = dk_canvas_hash(ctx2.cells, ctx2.cells_count);

    printf("  로드: %s\n", cvp_strerror(st2));
    printf("  원본 해시: 0x%08X\n", hash1);
    printf("  로드 해시: 0x%08X\n", hash_loaded);
    printf("  일치 = %s\n", hash1 == hash_loaded ? "YES (결정론 보존)" : "NO (손상!)");
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 8: Tervas 읽기 전용 조회
     *
     * Tervas는 엔진 상태를 수정하지 않고 보기만 한다 (R-4).
     * 스냅샷을 찍고 → Projection → TvRenderCell[]로 변환한다.
     * ────────────────────────────────────────────────────── */
    printf("[STEP 8] Tervas 읽기 전용 조회 (R-4)\n");

    Tervas tv;
    tervas_init(&tv);
    tervas_bridge_snapshot(&tv, &ctx, ctx.tick);

    /* WH 영역 Projection */
    TvFrame frame;
    TvFilter flt;
    tv_filter_reset(&flt);
    flt.mode = TV_PROJ_WH;
    tv_build_frame(&frame, &tv.snapshot, &flt, 64, 32);

    printf("  스냅샷 tick: %u\n", frame.tick);
    printf("  WH 활성 셀: %u\n", frame.wh_active);
    printf("  BH 활성 셀: %u\n", frame.bh_active);

    /* inspect: HELLO 확인 */
    printf("\n  [적층 데이터 조회]\n");
    for (int i = 0; hello[i]; i++) {
        char buf[256];
        tervas_bridge_inspect(&ctx, 512, 512 + (uint32_t)i, buf, sizeof(buf));
        printf("  %s\n", buf);
    }

    /* 해시 불변 확인 */
    uint32_t hash_after = dk_canvas_hash(ctx.cells, ctx.cells_count);
    printf("\n  Tervas 조회 후 해시: 0x%08X\n", hash_after);
    printf("  불변 = %s (R-4 준수)\n",
           hash1 == hash_after ? "YES" : "NO (Tervas가 상태를 수정함!)");

    tervas_free(&tv);
    print_sep();

    /* ──────────────────────────────────────────────────────
     * STEP 9: 요약
     * ────────────────────────────────────────────────────── */
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║              실행 완료 요약              ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║  적층 데이터    : HELLO (5셀, Y축 방향)  ║\n");
    printf("║  결정론 해시    : 0x%08X            ║\n", hash1);
    printf("║  CVP 저장/로드  : 해시 일치 %s          ║\n",
           hash1 == hash_loaded ? "YES" : "NO ");
    printf("║  Tervas R-4     : 불변 %s               ║\n",
           hash1 == hash_after ? "YES" : "NO ");
    printf("║  Phase-6 tick   : %u                     ║\n", ctx.tick);
    printf("║  WH 활성 셀    : %u                      ║\n", frame.wh_active);
    printf("╚══════════════════════════════════════════╝\n\n");

    /* cleanup */
    remove("hello.cvp");
    return 0;
}
