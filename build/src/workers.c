/*
 * workers.c — Phase 6: Lane 기반 멀티스레드 실행
 *
 * [W-1] lane 범위 분리 (셀 충돌 없음)
 * [W-2] pthread_barrier 틱 경계 강제  
 * [W-3] merge는 lane_id 오름차순 (결정론)
 */
#include "../include/canvasos_workers.h"
#include "../include/canvas_determinism.h"
#include "../include/engine_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void assign_lane_ranges(WorkerPool *pool) {
    int tc = pool->thread_count;
    int lanes_per = LANE_ID_MAX / tc;
    int remainder = LANE_ID_MAX % tc;
    int start = 0;
    for (int i = 0; i < tc; i++) {
        int cnt = lanes_per + (i < remainder ? 1 : 0);
        pool->workers[i].worker_id  = i;
        pool->workers[i].lane_start = (uint8_t)start;
        pool->workers[i].lane_end   = (uint8_t)(start + cnt);
        start += cnt;
    }
}


static int sjbarrier_step_wait(SJBarrier* b) {
    unsigned phase;
    pthread_mutex_lock(&b->mtx);
    phase = b->phase;
    b->waiting++;
    if (b->waiting == b->count) {
        b->waiting = 0;
        b->phase++;
        pthread_cond_broadcast(&b->cv);
    } else {
        while (phase == b->phase) pthread_cond_wait(&b->cv, &b->mtx);
    }
    pthread_mutex_unlock(&b->mtx);
    return 0;
}

int sjbarrier_init(SJBarrier* b, unsigned count) {
    if (!b || count == 0) return -1;
    memset(b, 0, sizeof(*b));
    b->count = count;
    if (pthread_mutex_init(&b->mtx, NULL) != 0) return -1;
    if (pthread_cond_init(&b->cv, NULL) != 0) {
        pthread_mutex_destroy(&b->mtx);
        return -1;
    }
    return 0;
}

void sjbarrier_destroy(SJBarrier* b) {
    if (!b) return;
    pthread_cond_destroy(&b->cv);
    pthread_mutex_destroy(&b->mtx);
}

void sjbarrier_wait(SJBarrier* b) {
    (void)sjbarrier_step_wait(b);
}

typedef struct { WorkerPool *pool; Worker *w; } WorkerArg;

static void *worker_thread(void *arg) {
    WorkerArg  *wa   = (WorkerArg *)arg;
    WorkerPool *pool = wa->pool;
    Worker     *w    = wa->w;
    free(wa);

    while (1) {
        /* 틱 시작 배리어 */
        sjbarrier_wait(&pool->barrier_start);

        if (!pool->running) {
            /* 종료 시에도 barrier_end를 wait해야 deadlock 방지 */
            sjbarrier_wait(&pool->barrier_end);
            break;
        }

        /* [W-1] 담당 lane 실행 */
        w->delta_count    = 0;
        w->cells_executed = 0;

        if (pool->lt) {
            for (int lid = w->lane_start; lid < w->lane_end; lid++) {
                int n = lane_tick(pool->ctx, pool->lt, (uint8_t)lid);
                w->cells_executed += (uint32_t)(n > 0 ? n : 0);
            }
        }
        w->ticks_run++;

        /* 틱 완료 배리어 */
        sjbarrier_wait(&pool->barrier_end);
    }
    return NULL;
}

int workers_init(WorkerPool *pool, EngineContext *ctx,
                 LaneTable *lt, int thread_count) {
    if (!pool || !ctx) return -1;
    memset(pool, 0, sizeof(*pool));

    if (thread_count <= 0) {
        long nc = sysconf(_SC_NPROCESSORS_ONLN);
        thread_count = (int)(nc > 0 ? nc : 1);
    }
    if (thread_count > WORKERS_MAX) thread_count = WORKERS_MAX;
    if (thread_count < 1) thread_count = 1;

    pool->ctx          = ctx;
    pool->lt           = lt;
    pool->thread_count = thread_count;
    pool->running      = true;
    pool->merge_cfg    = merge_config_default();
    pool->merge_cfg.policy = MERGE_LAST_WINS;

    /* LaneTable 기본 */
    static LaneTable s_lt_a, s_lt_b;
    static int s_lt_idx = 0;
    LaneTable *def_lt = (s_lt_idx++ % 2 == 0) ? &s_lt_a : &s_lt_b;
    if (!pool->lt) {
        lane_table_init(def_lt);
        for (int i = 0; i < 256; i++) {
            LaneDesc ld = { .lane_id=(uint8_t)i, .flags=LANE_F_ACTIVE };
            lane_register(def_lt, &ld);
        }
        pool->lt = def_lt;
    }

    assign_lane_ranges(pool);

    /* barrier: thread_count + 1 (main도 참여) */
    if (sjbarrier_init(&pool->barrier_start, (unsigned)(thread_count + 1)) != 0) return -1;
    if (sjbarrier_init(&pool->barrier_end,   (unsigned)(thread_count + 1)) != 0) {
        sjbarrier_destroy(&pool->barrier_start);
        return -1;
    }

    for (int i = 0; i < thread_count; i++) {
        WorkerArg *wa = malloc(sizeof(WorkerArg));
        wa->pool = pool;
        wa->w    = &pool->workers[i];
        if (pthread_create(&pool->workers[i].thread, NULL,
                           worker_thread, wa) != 0) {
            free(wa);
            /* 이미 생성된 스레드 종료 */
            pool->running = false;
            pool->thread_count = i;
            /* 나머지 배리어 슬롯 채우기 */
            sjbarrier_wait(&pool->barrier_start);
            sjbarrier_wait(&pool->barrier_end);
            return -1;
        }
    }
    return 0;
}

int workers_run_tick(WorkerPool *pool) {
    if (!pool || !pool->running) return -1;

    /* 틱 시작 배리어 → 워커 실행 */
    sjbarrier_wait(&pool->barrier_start);

    /* 워커들 실행 중... */

    /* 틱 완료 배리어 */
    sjbarrier_wait(&pool->barrier_end);

    /* [W-3] merge: lane_id 오름차순 */
    for (int i = 0; i < pool->thread_count; i++) {
        Worker *w = &pool->workers[i];
        if (w->delta_count > 0)
            merge_run(pool->ctx, w->delta_buf, w->delta_count, pool->merge_cfg);
        pool->total_cells += w->cells_executed;
    }

    engctx_tick(pool->ctx);
    pool->total_ticks++;
    return 0;
}

int workers_run_ticks(WorkerPool *pool, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        int r = workers_run_tick(pool);
        if (r) return r;
    }
    return 0;
}

void workers_destroy(WorkerPool *pool) {
    if (!pool) return;
    pool->running = false;
    /* 스레드들이 barrier_start에서 깨어나 종료 처리 후 barrier_end까지 대기 */
    sjbarrier_wait(&pool->barrier_start);
    sjbarrier_wait(&pool->barrier_end);
    for (int i = 0; i < pool->thread_count; i++)
        pthread_join(pool->workers[i].thread, NULL);
    sjbarrier_destroy(&pool->barrier_start);
    sjbarrier_destroy(&pool->barrier_end);
    memset(pool, 0, sizeof(*pool));
}

void workers_print_stats(const WorkerPool *pool) {
    printf("=== WorkerPool ===\n  threads=%d  ticks=%llu  cells=%llu\n",
           pool->thread_count,
           (unsigned long long)pool->total_ticks,
           (unsigned long long)pool->total_cells);
    for (int i = 0; i < pool->thread_count; i++) {
        const Worker *w = &pool->workers[i];
        printf("  [W%d] lane=%u..%u  ticks=%u\n",
               i, w->lane_start, w->lane_end, w->ticks_run);
    }
}

uint32_t workers_canvas_hash(const WorkerPool *pool) {
    return dk_canvas_hash(pool->ctx->cells, pool->ctx->cells_count);
}
