#pragma once
/*
 * canvasos_proc.h — Phase-8: Process Model
 *
 * fork/exec/exit/wait 완성.
 * Process = 에너지 예산 + 게이트 공간 + 코드 타일 + parent-child.
 * 모든 변이는 WH에 기록된다.
 */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvasos_sched.h"

/* ── 프로세스 확장 필드 (Process에 추가) ──────────────────── */
typedef struct {
    uint32_t  pid;
    uint32_t  parent_pid;       /* fork 계보, 0=init          */
    ProcState state;
    GateSpace space;
    uint16_t  code_tile;        /* 코드 적재 타일 시작         */
    uint16_t  code_tiles;       /* 코드 타일 수               */
    uint16_t  stack_tile;       /* 스택 영역 타일             */
    uint32_t  energy;
    uint32_t  energy_max;
    uint32_t  tick_born;
    uint32_t  tick_last;
    uint8_t   exit_code;        /* proc_exit에서 설정          */
    uint8_t   lane_id;          /* 소속 Lane = uid             */
    uint8_t   sig_pending;      /* T8-02 시그널 비트마스크     */
    uint8_t   sig_mask;
    uint8_t   flags;
} Proc8;

#define PROC8_MAX  256    /* Phase-8: 64 → 256 확장 */
#define PID_INIT   0      /* init 프로세스 */
#define PID_SHELL  1      /* 셸 프로세스 */

/* ── 프로세스 테이블 ─────────────────────────────────────── */
typedef struct {
    Proc8     procs[PROC8_MAX];
    uint16_t  freelist[PROC8_MAX];
    uint32_t  free_head;
    uint32_t  free_count;
    uint32_t  next_pid;
    uint32_t  count;           /* 활성 프로세스 수 */
    EngineContext *ctx;
} ProcTable;

/* ── WH opcodes (Phase-8) ──────────────────────────────── */
#define WH_OP_PROC_SPAWN  0x75
#define WH_OP_PROC_EXIT   0x76
#define WH_OP_PROC_EXEC   0x77

/* ── API ─────────────────────────────────────────────────── */

void proctable_init(ProcTable *pt, EngineContext *ctx);

/*
 * proc_spawn — 자식 프로세스 생성
 *
 * parent_pid: 부모 pid (0=init에서 생성)
 * code_tile:  코드가 적재된 타일 시작 번호
 * energy:     초기 에너지
 * lane_id:    소속 Lane (=uid)
 *
 * returns: 새 pid, 또는 -1 (테이블 가득)
 *
 * 규칙:
 *   - 부모의 게이트 공간에서 자식 공간 분리
 *   - WH에 PROC_SPAWN 레코드 기록
 *   - BH에 에너지 설정
 */
int proc_spawn(ProcTable *pt, uint32_t parent_pid,
               uint16_t code_tile, uint32_t energy, uint8_t lane_id);

/*
 * proc_exec — 코드 타일 교체 (exec)
 *
 * 현재 프로세스의 code_tile을 교체한다.
 * 이전 코드 타일은 해제하지 않는다 (caller 책임).
 *
 * returns: 0=OK, -1=pid not found
 */
int proc_exec(ProcTable *pt, uint32_t pid, uint16_t new_code_tile);

/*
 * proc_exit — 프로세스 종료
 *
 * ZOMBIE 상태 전환.
 * 자식 프로세스 → init(pid=0)에 입양.
 * 부모에게 SIG_CHILD 전송.
 * 게이트 CLOSE.
 *
 * returns: 0=OK, -1=pid not found
 */
int proc_exit(ProcTable *pt, uint32_t pid, uint8_t exit_code);

/*
 * proc_wait — ZOMBIE 자식 회수
 *
 * parent_pid의 ZOMBIE 자식 중 하나를 찾아서 슬롯 해제.
 * status에 exit_code 기록.
 *
 * returns: 회수된 자식 pid, 또는 -1 (ZOMBIE 없음)
 */
int proc_wait(ProcTable *pt, uint32_t parent_pid, uint8_t *status);

/*
 * proc_tick — 매 tick에 호출
 *
 * 모든 RUNNING 프로세스의 에너지를 1 감소.
 * 에너지 0 → SLEEPING + 게이트 CLOSE.
 * 시그널 확인.
 *
 * returns: 이번 tick에 실행된 프로세스 수
 */
int proc_tick(ProcTable *pt);

/* 조회 */
Proc8 *proc_find(ProcTable *pt, uint32_t pid);
int    proc_count_children(ProcTable *pt, uint32_t parent_pid);
void   proc_dump(const ProcTable *pt);  /* 디버그 출력 */
