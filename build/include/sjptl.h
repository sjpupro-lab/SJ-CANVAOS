#pragma once
/*
 * sjptl.h — SJ Pixel-Terminal Language (SJ-PTL) v0.1
 *
 * =====================================================
 * SJ-PTL: 타자기/피아노 입력 규칙
 * =====================================================
 *
 * 모든 입력은 "토큰 스트림"이다.
 * 커서(x,y)가 현재 편집 위치 = 캔버스 셀 하나.
 *
 * 레지스터 (편집 버퍼):
 *   A(32비트), B(8비트), G(8비트), R(8비트)
 *   현재 커서 위치의 셀에 쓸 내용을 조립하는 임시 공간.
 *
 * 건반(토큰):
 * ─────────────────────────────────────────────────────
 *  이동
 *    ^      커서 위(y-1)
 *    v      커서 아래(y+1)
 *    <      커서 왼쪽(x-1)
 *    >      커서 오른쪽(x+1)
 *    .N     커서 오른쪽으로 N칸  (예: .5)
 *    ,N     커서 아래로 N칸       (예: ,3)
 *    :X,Y   커서 절대 이동 (예: :512,512)
 *
 *  레지스터 설정
 *    A=HHHHHHHH  A 채널 설정 (hex 32비트)  예: A=00010001
 *    A+HH        A += 값                   예: A+01
 *    B=HH        B 채널 설정 (hex 8비트)   예: B=10
 *    G=DD        G 채널 설정 (decimal)     예: G=200
 *    R=HH        R 채널 설정 (hex or char) 예: R=41 or R='A'
 *
 *  블록 선택
 *    bL     블록 Left 경계 (현재 X 고정)
 *    bR     블록 Right 경계
 *    bT     블록 Top 경계 (현재 Y 고정)
 *    bB     블록 Bottom 경계
 *    bW N   블록 Width = N 셀
 *    bH N   블록 Height = N 셀
 *
 *  커밋
 *    !      현재 레지스터 → 현재 셀에 기록 + WH_EDIT_COMMIT + Y auto-step
 *    !!N    N회 반복 커밋 (y축 N칸 진행)
 *    !B     블록 전체 채우기 (B/G/R만 적용, A는 유지)
 *
 *  저장/복원/재현
 *    sv [file]       CVP_SAVE
 *    ld [file]       CVP_LOAD
 *    rp FROM TO      WH_REPLAY [from_tick, to_tick]
 *
 *  틱
 *    tk [N]          TICK × N
 *
 *  게이트
 *    go GATE_ID      GATE_OPEN
 *    gc GATE_ID      GATE_CLOSE
 *
 *  BH
 *    be PID E        BH set energy
 *    bd PID D        BH decay
 *
 *  조회
 *    ?              현재 커서 셀 정보
 *    ps             프로세스 목록
 *    wl [N]         WH log N
 *    info           엔진 상태
 *    q / quit       SJTerm 종료
 *
 * ─────────────────────────────────────────────────────
 *
 * 예시 세션:
 *   :512,512   → 커서를 (512,512)로
 *   B=01 G=64 R=41  → 레지스터: opcode=PRINT energy=100 payload='A'
 *   !          → 커밋: 셀에 기록, WH에 EDIT_COMMIT 저장, y+1
 *   R=42 !     → 'B' 커밋
 *   sv         → session.cvp 저장
 *   rp 0 5     → tick 0~5 리플레이
 *
 * =====================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "engine_time.h"

/* ── WH opcode 확장: SJTerm 커밋 기록용 ── */
#define WH_OP_EDIT_COMMIT  0x60u   /* SJTerm 커밋 이벤트 */
#define WH_OP_EDIT_BLOCK   0x61u   /* 블록 채우기 이벤트 */

/* ── 토큰 종류 ── */
typedef enum {
    TOK_MOVE_UP = 1,   /* ^ */
    TOK_MOVE_DN,       /* v */
    TOK_MOVE_LT,       /* < */
    TOK_MOVE_RT,       /* > */
    TOK_MOVE_RT_N,     /* .N */
    TOK_MOVE_DN_N,     /* ,N */
    TOK_MOVE_ABS,      /* :X,Y */

    TOK_SET_A,         /* A=HHHHHHHH */
    TOK_ADD_A,         /* A+HH */
    TOK_SET_B,         /* B=HH */
    TOK_SET_G,         /* G=DD */
    TOK_SET_R,         /* R=HH */

    TOK_BLOCK_L,       /* bL */
    TOK_BLOCK_R,       /* bR */
    TOK_BLOCK_T,       /* bT */
    TOK_BLOCK_B,       /* bB */
    TOK_BLOCK_W,       /* bW N */
    TOK_BLOCK_H,       /* bH N */

    TOK_COMMIT,        /* ! */
    TOK_COMMIT_N,      /* !!N */
    TOK_COMMIT_BLOCK,  /* !B */

    TOK_SAVE,          /* sv [file] */
    TOK_LOAD,          /* ld [file] */
    TOK_REPLAY,        /* rp FROM TO */
    TOK_TICK,          /* tk [N] */

    TOK_GATE_OPEN,     /* go GATE_ID */
    TOK_GATE_CLOSE,    /* gc GATE_ID */

    TOK_BH_SET,        /* be PID E */
    TOK_BH_DECAY,      /* bd PID D */

    TOK_QUERY_CELL,    /* ? */
    TOK_QUERY_PS,      /* ps */
    TOK_QUERY_WL,      /* wl [N] */
    TOK_QUERY_INFO,    /* info */
    TOK_QUIT,          /* q / quit */
    TOK_HELP,          /* help */

    TOK_UNKNOWN,
    TOK_EOF,
} PtlTokenKind;

/* ── 파싱된 토큰 ── */
typedef struct {
    PtlTokenKind kind;
    union {
        int32_t  i;          /* 정수 인자 (N, gate_id, etc.) */
        uint32_t u;          /* 부호없는 인자 (A, B, G, R) */
        struct { int32_t x, y; } pos;  /* :X,Y */
        struct { uint32_t pid; uint8_t val; } bh; /* be/bd */
        struct { int32_t from, to; } rp;  /* rp FROM TO */
        char file[64];       /* sv/ld 파일명 */
    };
} PtlToken;

/* ── 커서 / 레지스터 상태 ── */
typedef struct {
    int32_t  cx, cy;         /* 현재 커서 좌표 */
    uint32_t reg_A;
    uint8_t  reg_B;
    uint8_t  reg_G;
    uint8_t  reg_R;

    /* 블록 선택 */
    int32_t  blk_x0, blk_y0;
    int32_t  blk_x1, blk_y1;
    bool     blk_active;

    /* auto-step 정책 */
    bool y_auto_step;        /* 커밋 후 y+1 */
    int32_t  auto_step_dx;   /* 수평 자동 진행 */
    int32_t  auto_step_dy;   /* 수직 자동 진행 (기본 +1) */

    /* WH 커밋 카운터 */
    uint32_t edit_count;
} PtlState;

/* ── 파서 API ── */

/* 한 줄(null-terminated)을 토큰 배열로 파싱.
 * out_toks: 결과 버퍼, max_toks: 최대 개수.
 * 반환: 파싱된 토큰 수 (0이면 빈 줄 or 주석) */
int ptl_parse_line(const char *line, PtlToken *out_toks, int max_toks);

/* 토큰 1개를 실행.
 * ctx: 엔진 컨텍스트
 * st: 커서/레지스터 상태
 * tok: 실행할 토큰
 * 반환: 0=계속, 1=quit, -1=error */
int ptl_exec_token(EngineContext *ctx, PtlState *st, const PtlToken *tok);

/* 한 줄 전체 실행 (parse + exec loop) */
int ptl_exec_line(EngineContext *ctx, PtlState *st, const char *line);

/* PtlState 초기화 */
void ptl_state_init(PtlState *st, int32_t start_x, int32_t start_y);

/* 커밋 1회 수행 (WH 기록 포함) */
int ptl_do_commit(EngineContext *ctx, PtlState *st);

/* 블록 커밋 수행 */
int ptl_do_commit_block(EngineContext *ctx, PtlState *st);
