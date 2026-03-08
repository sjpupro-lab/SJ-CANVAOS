#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasfs.h"

#include "canvasos_engine_ctx.h"

/* =====================================================
 * Phase 3 — Process Scheduler
 *
 * 설계 원칙:
 *   - 점프/컨텍스트 스위치 없음
 *   - 프로세스 = "에너지 예산 + 게이트 공간"
 *   - 격리 단위: VOLH+VOLT 쌍 (gate_space)  ※ VOLH(0,1).A = volt_gate_id
 *   - 에너지 0 → sleep, 게이트 닫힘 → I/O 차단
 *   - tick() 호출만으로 전체 프로세스 스텝 진행
 *
 * Phase 4/5 연동 포인트:
 *   - Phase 4 CVP: proc.cvp_fd = 로드된 CVP 섹션 핸들
 *   - Phase 5 Mount: proc.mount_ref = 원격 Canvas 참조
 *
 * 스켈레톤 상태:
 *   sched_spawn  ✓ 구현
 *   sched_tick   ✓ 구현 (에너지 감소, sleep)
 *   sched_recharge ✓ 구현
 *   sched_owner  ✓ 구현
 *   sched_ipc    ☐ Phase 5에서 추가 (Canvas간 메시지)
 *   sched_snapshot ☐ Phase 4에서 추가 (CVP 저장)
 * ===================================================== */

/* ---- 프로세스 상태 ---- */
typedef enum {
    PROC_RUNNING  = 0,   /* 에너지 있고 게이트 열림     */
    PROC_SLEEPING = 1,   /* 에너지 소진 → 자동 sleep    */
    PROC_BLOCKED  = 2,   /* 게이트 CLOSED → I/O 차단    */
    PROC_ZOMBIE   = 3,   /* 종료, 슬롯 회수 대기        */
} ProcState;

/* ---- 게이트 공간: VOLH+VOLT 쌍 ---- */
typedef struct {
    uint16_t volh;    /* VOLH tile gate_id */
    uint16_t volt;    /* VOLT tile gate_id (VOLH에서 파생) */
} GateSpace;

/* ---- 프로세스 디스크립터 ---- */
typedef struct {
    uint32_t    pid;            /* 프로세스 ID (monotonic)          */
    ProcState   state;
    GateSpace   space;          /* 이 프로세스가 소유한 게이트 공간  */
    uint32_t    energy;         /* 남은 에너지 (0 → sleep)          */
    uint32_t    energy_max;     /* 최대 에너지 (recharge 한계)       */
    uint32_t    tick_born;      /* 생성 틱                          */
    uint32_t    tick_last;      /* 마지막 실행 틱                   */
    /* Phase 4 연동 포인트 */
    uint32_t    cvp_section;    /* CVP 섹션 오프셋 (0=없음)         */
    /* Phase 5 연동 포인트 */
    uint16_t    mount_canvas;   /* 원격 Canvas ID (0xFFFF=없음)     */
    uint32_t    ipc_cursor;     /* last IPC tick consumed (deterministic) */
    uint8_t     flags;          /* 예약                             */
    uint8_t     pad;
} Process;

#define PROC_MAX 64

/* ---- 스케줄러 ---- */
typedef struct {
    Process  procs[PROC_MAX];
    uint32_t count;
    uint32_t next_pid;
    uint32_t tick;
    ActiveSet *aset;            /* 게이트 열기/닫기 권한 (fast bitmap) */
    EngineContext *ctx;  /* optional: formal gate ops + WH/BH */
} Scheduler;

/* =====================================================
 * API
 * ===================================================== */

void sched_init(Scheduler *sc, ActiveSet *aset);
void sched_bind_ctx(Scheduler *sc, EngineContext *ctx);

/* 프로세스 생성: VOLH 게이트 공간 할당 + 에너지 주입
 * returns pid, or -1 if full */
int  sched_spawn(Scheduler *sc, GateSpace space,
                 uint32_t energy, uint32_t energy_max);

/* 틱 진행: 모든 RUNNING 프로세스 에너지 1 감소
 * 에너지 0 → SLEEPING + 게이트 닫기
 * returns: 이번 틱에 실행된 프로세스 수 */
int  sched_tick(Scheduler *sc);

/* 에너지 충전: SLEEPING이면 RUNNING으로 깨움 */
void sched_recharge(Scheduler *sc, uint32_t pid, uint32_t amount);

/* 강제 종료: ZOMBIE로 표시 + 게이트 닫기 */
void sched_kill(Scheduler *sc, uint32_t pid);

/* (x,y) 좌표가 어느 프로세스 공간에 속하는지
 * VOLT 타일 범위 기준으로 판단
 * returns pid or -1 */
int  sched_owner(const Scheduler *sc, uint16_t x, uint16_t y);

/* 상태 덤프 (디버그) */
void sched_dump(const Scheduler *sc);

/* ---- Phase 4 stub ---- */
/* CVP 섹션 참조 등록 (Phase 4에서 구현) */
void sched_set_cvp(Scheduler *sc, uint32_t pid, uint32_t cvp_section);

/* ---- Phase 5 stub ---- */
/* Canvas간 IPC 메시지 전송 (Phase 5에서 구현)
 * src_pid → dst_canvas:dst_pid, payload in FsKey */
typedef struct {
    uint32_t src_pid;
    uint16_t dst_canvas;
    uint32_t dst_pid;
    FsKey    payload_key;  /* 메시지 데이터 위치 */
} IpcMsg;

int sched_ipc_send(Scheduler *sc, const IpcMsg *msg);   /* Phase 5 stub */
int sched_ipc_recv(Scheduler *sc, uint32_t pid, IpcMsg *out); /* Phase 5 stub */
