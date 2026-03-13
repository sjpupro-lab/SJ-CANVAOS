/* syscall.c — Phase-8→11: System Call Dispatch + All Tier Handlers
 *
 * Phase-11 변경:
 *   - COS_ENOSYS 통일 (기존 -38 제거)
 *   - Tier-1: SPAWN, EXIT, WAIT, KILL, SIGNAL, GETPPID
 *   - Tier-2: OPEN, READ, WRITE, CLOSE, SEEK, MKDIR, RM, LS
 *   - Tier-3: PIPE, DUP
 *   - Tier-4: GATE_OPEN, GATE_CLOSE, MPROTECT, HASH, TIMEWARP, DET_MODE, SNAPSHOT
 *   - 모든 핸들러에 wh_record_syscall 호출
 */
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_errno.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvasos_permission.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include <string.h>

static SyscallHandler g_handlers[SYS_MAX];

/* ── 커널 테이블 포인터 (launcher에서 설정) ──── */
static ProcTable *g_sys_pt   = NULL;
static PipeTable *g_sys_pipe = NULL;

void syscall_set_tables(ProcTable *pt, PipeTable *pipe) {
    g_sys_pt   = pt;
    g_sys_pipe = pipe;
}

/* ── WH 기록 ──────────────────────────────── */
static void syscall_wh(EngineContext *ctx, uint32_t pid, uint8_t nr, uint8_t ok) {
    if (!ctx) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index  = WH_OP_SYSCALL;
    r.param0        = nr;
    r.target_addr   = pid;
    r.target_kind   = WH_TGT_PROC;
    r.flags         = ok;
    wh_write_record(ctx, ctx->tick, &r);
}

/* ══════════════════════════════════════════════
 * 기존 Phase-8 핸들러
 * ══════════════════════════════════════════════ */
static int handler_getpid(EngineContext *ctx, uint32_t pid,
                          uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)a0; (void)a1; (void)a2;
    return (int)pid;
}

static int handler_time(EngineContext *ctx, uint32_t pid,
                        uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a0; (void)a1; (void)a2;
    return ctx ? (int)ctx->tick : COS_ENOSYS;
}

static int handler_tick(EngineContext *ctx, uint32_t pid,
                        uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a0; (void)a1; (void)a2;
    return engctx_tick(ctx);
}

/* ══════════════════════════════════════════════
 * Sprint 1 — Tier-1: 프로세스 제어
 * ══════════════════════════════════════════════ */

/* SYS_SPAWN(0x01): a0=code_tile, a1=energy, a2=lane_id */
static int handler_spawn(EngineContext *ctx, uint32_t pid,
                          uint32_t a0, uint32_t a1, uint32_t a2) {
    if (!g_sys_pt) return COS_ENOSYS;
    uint16_t code_tile = (uint16_t)(a0 & 0xFFFF);
    uint32_t energy    = a1 > 0 ? a1 : 100;
    uint8_t  lane_id   = (uint8_t)(a2 & 0xFF);

    /* Sprint 5: 부모 권한 상속 — 비root 프로세스는 자신의 lane에만 spawn 가능 */
    Proc8 *parent = proc_find(g_sys_pt, pid);
    if (parent && !perm_is_root(parent->lane_id)) {
        lane_id = parent->lane_id; /* 부모의 lane 강제 상속 */
        /* energy도 부모 잔여 energy 이내로 제한 */
        if (energy > parent->energy)
            energy = parent->energy;
    }

    int child = proc_spawn(g_sys_pt, pid, code_tile, energy, lane_id);
    syscall_wh(ctx, pid, SYS_SPAWN, (uint8_t)(child >= 0));
    return child;
}

/* SYS_EXIT(0x02): a0=exit_code */
static int handler_exit(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    if (!g_sys_pt) return COS_ENOSYS;
    int rc = proc_exit(g_sys_pt, pid, (uint8_t)(a0 & 0xFF));
    syscall_wh(ctx, pid, SYS_EXIT, (uint8_t)(rc >= 0));
    return rc;
}

/* SYS_WAIT(0x03): 블로킹 wait — ZOMBIE 자식 스캔, 없으면 BLOCKED 전환 */
static int handler_wait(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    if (!g_sys_pt) return COS_ENOSYS;
    uint8_t status = 0;
    int child = proc_wait(g_sys_pt, pid, &status);
    if (child < 0) {
        /* 블로킹: BLOCKED로 전환, proc_tick에서 자식 ZOMBIE 감지 시 깨움 */
        Proc8 *parent = proc_find(g_sys_pt, pid);
        if (parent && parent->state == PROC_RUNNING)
            parent->state = PROC_BLOCKED;
    }
    syscall_wh(ctx, pid, SYS_WAIT, (uint8_t)(child >= 0));
    return child >= 0 ? child : COS_ESRCH;
}

/* SYS_KILL(0x04): a0=target_pid, a1=signal */
static int handler_kill(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    if (!g_sys_pt) return COS_ENOSYS;
    /* 권한 검증: uid=0 또는 소유자만 kill 가능 */
    Proc8 *src = proc_find(g_sys_pt, pid);
    Proc8 *dst = proc_find(g_sys_pt, a0);
    if (!dst) return COS_ESRCH;
    if (src && !perm_is_root(src->lane_id) && src->lane_id != dst->lane_id)
        return COS_EPERM;
    int rc = sig_send(g_sys_pt, a0, (uint8_t)(a1 & 0xFF));
    syscall_wh(ctx, pid, SYS_KILL, (uint8_t)(rc >= 0));
    return rc;
}

/* SYS_SIGNAL(0x05): a0=signal, a1=action (0=default,1=mask) */
static int handler_signal(EngineContext *ctx, uint32_t pid,
                           uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    if (!g_sys_pt) return COS_ENOSYS;
    uint8_t sig = (uint8_t)(a0 & 0xFF);
    if (a1 == 1)
        sig_mask_set(g_sys_pt, pid, (uint8_t)(1u << (sig - 1u)));
    else
        sig_mask_clear(g_sys_pt, pid, (uint8_t)(1u << (sig - 1u)));
    syscall_wh(ctx, pid, SYS_SIGNAL, 1);
    return 0;
}

/* SYS_GETPPID(0x42) */
static int handler_getppid(EngineContext *ctx, uint32_t pid,
                            uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    if (!g_sys_pt) return COS_ENOSYS;
    Proc8 *p = proc_find(g_sys_pt, pid);
    if (!p) return COS_ESRCH;
    syscall_wh(ctx, pid, SYS_GETPPID, 1);
    return (int)p->parent_pid;
}

/* ══════════════════════════════════════════════
 * Sprint 2 — Tier-2: 파일 I/O
 * ══════════════════════════════════════════════ */

/* SYS_OPEN(0x10): a0=path(unused), a1=flags → fd */
static int handler_open(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a2;
    int fd = fd_open(ctx, pid, NULL, (uint8_t)(a1 & 0xFF));
    syscall_wh(ctx, pid, SYS_OPEN, (uint8_t)(fd >= 0));
    return fd >= 0 ? fd : COS_EBADF;
}

/* SYS_READ(0x11): a0=fd, a1=unused, a2=len */
static int handler_read(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1;
    uint8_t tmp[256];
    uint16_t len = (uint16_t)(a2 > 256 ? 256 : a2);
    int rc = fd_read(ctx, pid, (int)a0, tmp, len);
    syscall_wh(ctx, pid, SYS_READ, (uint8_t)(rc >= 0));
    return rc;
}

/* SYS_WRITE(0x12): a0=fd, a1=unused, a2=len */
static int handler_write(EngineContext *ctx, uint32_t pid,
                          uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1;
    uint8_t tmp[256];
    uint16_t len = (uint16_t)(a2 > 256 ? 256 : a2);
    int rc = fd_write(ctx, pid, (int)a0, tmp, len);
    syscall_wh(ctx, pid, SYS_WRITE, (uint8_t)(rc >= 0));
    return rc;
}

/* SYS_CLOSE(0x13): a0=fd */
static int handler_close(EngineContext *ctx, uint32_t pid,
                          uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    int rc = fd_close(ctx, pid, (int)a0);
    syscall_wh(ctx, pid, SYS_CLOSE, (uint8_t)(rc >= 0));
    return rc >= 0 ? 0 : COS_EBADF;
}

/* SYS_SEEK(0x14): a0=fd, a1=offset */
static int handler_seek(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    int rc = fd_seek(ctx, pid, (int)a0, (uint16_t)(a1 & 0xFFFF));
    syscall_wh(ctx, pid, SYS_SEEK, (uint8_t)(rc >= 0));
    return rc >= 0 ? 0 : COS_EBADF;
}

/* SYS_MKDIR(0x15), SYS_RM(0x16), SYS_LS(0x17) — 셸 명령 연동 */
static int handler_mkdir(EngineContext *ctx, uint32_t pid,
                          uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    syscall_wh(ctx, pid, SYS_MKDIR, 1);
    return 0; /* shell 경유 시 cmd_mkdir에서 처리 */
}

static int handler_rm(EngineContext *ctx, uint32_t pid,
                       uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    syscall_wh(ctx, pid, SYS_RM, 1);
    return 0;
}

static int handler_ls(EngineContext *ctx, uint32_t pid,
                       uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    syscall_wh(ctx, pid, SYS_LS, 1);
    return 0;
}

/* ══════════════════════════════════════════════
 * Sprint 3 — Tier-3: IPC
 * ══════════════════════════════════════════════ */

/* SYS_PIPE(0x20): → pipe_id */
static int handler_pipe(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    if (!g_sys_pipe) return COS_ENOSYS;
    int pipe_id = pipe_create(g_sys_pipe, ctx, pid, pid);
    syscall_wh(ctx, pid, SYS_PIPE, (uint8_t)(pipe_id >= 0));
    return pipe_id >= 0 ? pipe_id : COS_EPIPE;
}

/* SYS_DUP(0x21): a0=old_fd, a1=new_fd */
static int handler_dup(EngineContext *ctx, uint32_t pid,
                        uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    int rc = fd_dup(ctx, pid, (int)a0, (int)a1);
    syscall_wh(ctx, pid, SYS_DUP, (uint8_t)(rc >= 0));
    return rc >= 0 ? (int)a1 : COS_EBADF;
}

/* ══════════════════════════════════════════════
 * Sprint 5 — Tier-4: CanvasOS 고유
 * ══════════════════════════════════════════════ */

/* SYS_GATE_OPEN(0x30): a0=gate_id — 소유권 검사 포함 */
static int handler_gate_open(EngineContext *ctx, uint32_t pid,
                              uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    /* Sprint 5: 프로세스의 gate space 내 gate만 허용 (root 제외) */
    if (g_sys_pt) {
        Proc8 *p = proc_find(g_sys_pt, pid);
        if (p && !perm_is_root(p->lane_id)) {
            uint16_t gid = (uint16_t)(a0 & 0xFFFF);
            if (gid != p->space.volh && gid != p->space.volt) {
                syscall_wh(ctx, pid, SYS_GATE_OPEN, 0);
                return COS_EPERM;
            }
        }
    }
    gate_open_tile(ctx, (uint16_t)(a0 & 0xFFFF));
    syscall_wh(ctx, pid, SYS_GATE_OPEN, 1);
    return 0;
}

/* SYS_GATE_CLOSE(0x31): a0=gate_id — 소유권 검사 포함 */
static int handler_gate_close(EngineContext *ctx, uint32_t pid,
                               uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    /* Sprint 5: 프로세스의 gate space 내 gate만 허용 (root 제외) */
    if (g_sys_pt) {
        Proc8 *p = proc_find(g_sys_pt, pid);
        if (p && !perm_is_root(p->lane_id)) {
            uint16_t gid = (uint16_t)(a0 & 0xFFFF);
            if (gid != p->space.volh && gid != p->space.volt) {
                syscall_wh(ctx, pid, SYS_GATE_CLOSE, 0);
                return COS_EPERM;
            }
        }
    }
    gate_close_tile(ctx, (uint16_t)(a0 & 0xFFFF));
    syscall_wh(ctx, pid, SYS_GATE_CLOSE, 1);
    return 0;
}

/* SYS_MPROTECT(0x32): root only */
static int handler_mprotect(EngineContext *ctx, uint32_t pid,
                              uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    if (g_sys_pt) {
        Proc8 *p = proc_find(g_sys_pt, pid);
        if (p && !perm_is_root(p->lane_id)) {
            syscall_wh(ctx, pid, SYS_MPROTECT, 0);
            return COS_EPERM;
        }
    }
    syscall_wh(ctx, pid, SYS_MPROTECT, 1);
    return 0;
}

/* SYS_HASH(0x44) */
static int handler_hash(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    uint32_t h = 0;
    if (ctx && ctx->cells)
        h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    syscall_wh(ctx, pid, SYS_HASH, 1);
    return (int)h;
}

/* SYS_TIMEWARP(0x50): a0=target_tick */
static int handler_timewarp(EngineContext *ctx, uint32_t pid,
                              uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    int rc = engctx_replay(ctx, a0, ctx->tick);
    syscall_wh(ctx, pid, SYS_TIMEWARP, (uint8_t)(rc >= 0));
    return rc;
}

/* SYS_DET_MODE(0x51): a0=on/off, root only */
static int handler_det_mode(EngineContext *ctx, uint32_t pid,
                              uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    if (g_sys_pt) {
        Proc8 *p = proc_find(g_sys_pt, pid);
        if (p && !perm_is_root(p->lane_id)) {
            syscall_wh(ctx, pid, SYS_DET_MODE, 0);
            return COS_EPERM;
        }
    }
    syscall_wh(ctx, pid, SYS_DET_MODE, 1);
    return 0; /* detmode 토글은 shell에서 직접 수행 */
}

/* SYS_SNAPSHOT(0x52) */
static int handler_snapshot(EngineContext *ctx, uint32_t pid,
                              uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a0; (void)a1; (void)a2;
    syscall_wh(ctx, pid, SYS_SNAPSHOT, 1);
    return (int)ctx->tick; /* snap_id = 현재 tick */
}

/* ══════════════════════════════════════════════
 * 초기화 + 디스패치
 * ══════════════════════════════════════════════ */

void syscall_init(void) {
    memset(g_handlers, 0, sizeof(g_handlers));

    /* Phase-8 기존 */
    syscall_register(SYS_GETPID,     handler_getpid);
    syscall_register(SYS_TIME,       handler_time);
    syscall_register(SYS_TICK,       handler_tick);

    /* Sprint 1: Tier-1 */
    syscall_register(SYS_SPAWN,      handler_spawn);
    syscall_register(SYS_EXIT,       handler_exit);
    syscall_register(SYS_WAIT,       handler_wait);
    syscall_register(SYS_KILL,       handler_kill);
    syscall_register(SYS_SIGNAL,     handler_signal);
    syscall_register(SYS_GETPPID,    handler_getppid);

    /* Sprint 2: Tier-2 */
    syscall_register(SYS_OPEN,       handler_open);
    syscall_register(SYS_READ,       handler_read);
    syscall_register(SYS_WRITE,      handler_write);
    syscall_register(SYS_CLOSE,      handler_close);
    syscall_register(SYS_SEEK,       handler_seek);
    syscall_register(SYS_MKDIR,      handler_mkdir);
    syscall_register(SYS_RM,         handler_rm);
    syscall_register(SYS_LS,         handler_ls);

    /* Sprint 3: Tier-3 */
    syscall_register(SYS_PIPE,       handler_pipe);
    syscall_register(SYS_DUP,        handler_dup);

    /* Sprint 5: Tier-4 */
    syscall_register(SYS_GATE_OPEN,  handler_gate_open);
    syscall_register(SYS_GATE_CLOSE, handler_gate_close);
    syscall_register(SYS_MPROTECT,   handler_mprotect);
    syscall_register(SYS_HASH,       handler_hash);
    syscall_register(SYS_TIMEWARP,   handler_timewarp);
    syscall_register(SYS_DET_MODE,   handler_det_mode);
    syscall_register(SYS_SNAPSHOT,   handler_snapshot);
}

int syscall_register(uint8_t nr, SyscallHandler handler) {
    if (nr >= SYS_MAX) return -1;
    g_handlers[nr] = handler;
    return 0;
}

int syscall_dispatch(EngineContext *ctx, uint32_t pid, uint8_t nr,
                     uint32_t a0, uint32_t a1, uint32_t a2) {
    if (nr >= SYS_MAX || !g_handlers[nr]) {
        syscall_wh(ctx, pid, nr, 0);
        return COS_ENOSYS;
    }
    return g_handlers[nr](ctx, pid, a0, a1, a2);
}
