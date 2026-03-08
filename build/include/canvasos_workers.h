#pragma once
/*
 * canvasos_workers.h — Phase 6: Lane 기반 멀티스레드 WorkerPool
 *
 * ===================================================================
 * 설계 원칙
 * ===================================================================
 *
 * [W-1] 각 워커는 고정된 lane_id 범위만 처리 (공유 쓰기 금지)
 *       lane이 캔버스를 독립 구역으로 나누므로 셀 충돌 없음.
 *
 * [W-2] 틱 경계(barrier)에서만 Δ-Commit/Merge/BH 수행 (DK-1 준수)
 *       모든 워커가 같은 tick에서 실행 → barrier 완료 후 다음 tick.
 *
 * [W-3] 결과 결정론: lane_id 오름차순으로 결과 merge
 *       thread_count=1 vs N → 동일 canvas hash (test_workers_determinism)
 *
 * [W-4] 워커 per-lane Δ 수집 → tick 끝 barrier → merge_run()으로 통합
 *
 * ===================================================================
 * 사용 예
 * ===================================================================
 *
 *   WorkerPool pool;
 *   workers_init(&pool, &ctx, 4);       // 4스레드
 *   workers_run_tick(&pool);            // 1틱 실행 (모든 lane 병렬)
 *   workers_run_tick(&pool);
 *   workers_destroy(&pool);
 *
 * ===================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* Portable barrier (pthread_barrier_t is not available on some libcs/build flags) */
typedef struct SJBarrier {
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    unsigned        count;
    unsigned        waiting;
    unsigned        phase;
} SJBarrier;

int sjbarrier_init(SJBarrier* b, unsigned count);
void sjbarrier_destroy(SJBarrier* b);
void sjbarrier_wait(SJBarrier* b);


#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvas_lane.h"
#include "canvas_merge.h"

/* ── 워커 1개 ── */
typedef struct {
    pthread_t       thread;
    uint8_t         lane_start;    /* 담당 lane 범위 [lane_start, lane_end) */
    uint8_t         lane_end;
    int             worker_id;

    /* Δ 수집 버퍼 (틱 경계 전까지 여기 쌓임) */
    Delta           delta_buf[MERGE_MAX_DELTAS];
    uint32_t        delta_count;

    /* 실행 결과 카운터 */
    uint32_t        cells_executed;
    uint32_t        ticks_run;
} Worker;

/* ── 워커 풀 ── */
#define WORKERS_MAX 16

typedef struct {
    Worker          workers[WORKERS_MAX];
    int             thread_count;

    /* 공유 엔진 컨텍스트 (읽기만 공유, 쓰기는 lane 격리) */
    EngineContext  *ctx;
    LaneTable      *lt;

    /* 틱 경계 배리어 */
    SJBarrier barrier_start;  /* 틱 시작 동기화 */
    SJBarrier barrier_end;    /* 틱 완료 동기화 */

    /* 제어 플래그 */
    volatile bool   running;
    volatile bool   do_tick;    /* 틱 실행 신호 */
    volatile int    ticks_todo; /* 남은 틱 수 */

    /* Merge 설정 */
    MergeConfig     merge_cfg;

    /* 전체 통계 */
    uint64_t        total_ticks;
    uint64_t        total_cells;
} WorkerPool;

/* ── API ── */

/*
 * workers_init: WorkerPool 초기화 + 스레드 생성.
 * thread_count: 0이면 CPU 코어 수 자동 감지.
 * lane_table: 없으면 내부에서 기본 LaneTable 생성.
 */
int  workers_init(WorkerPool *pool, EngineContext *ctx,
                  LaneTable *lt, int thread_count);

/*
 * workers_run_tick: 1틱을 모든 스레드가 병렬 실행.
 * [W-2] 완료 후 barrier → merge_run() → engctx_tick() 순서 고정.
 */
int  workers_run_tick(WorkerPool *pool);

/*
 * workers_run_ticks: N틱 연속 실행.
 */
int  workers_run_ticks(WorkerPool *pool, uint32_t n);

/*
 * workers_destroy: 스레드 종료 + 리소스 해제.
 */
void workers_destroy(WorkerPool *pool);

/*
 * workers_print_stats: 성능 통계 출력.
 */
void workers_print_stats(const WorkerPool *pool);

/*
 * workers_canvas_hash: 현재 canvas 해시 (결정론 검증용).
 */
uint32_t workers_canvas_hash(const WorkerPool *pool);
