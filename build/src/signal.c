/* signal.c — Phase-8: Signal System */
#include "../include/canvasos_signal.h"
#include "../include/engine_time.h"
#include "../include/canvasos_gate_ops.h"
#include <string.h>

static void sig_write_wh(ProcTable *pt, const Proc8 *p, uint8_t signal) {
    if (!pt || !pt->ctx || !p) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = pt->ctx->tick;
    r.opcode_index = WH_OP_SIG_SEND;
    r.param0 = signal;
    r.target_addr = p->pid;
    r.target_kind = WH_TGT_PROC;
    wh_write_record(pt->ctx, pt->ctx->tick, &r);
}

int sig_send(ProcTable *pt, uint32_t dst_pid, uint8_t signal) {
    if (!pt || signal == 0 || signal > SIG_MAX) return -1;
    Proc8 *p = proc_find(pt, dst_pid);
    if (!p) return -1;

    sig_write_wh(pt, p, signal);

    switch (signal) {
    case SIG_KILL:
        return proc_exit(pt, dst_pid, 128u + signal);
    case SIG_STOP:
        p->state = PROC_SLEEPING;
        gate_close_space_ctx(pt->ctx, p->space);
        return 0;
    case SIG_CONT:
        if (p->state == PROC_SLEEPING || p->state == PROC_BLOCKED) {
            p->state = PROC_RUNNING;
            gate_open_space_ctx(pt->ctx, p->space);
        }
        return 0;
    default:
        p->sig_pending |= SIG_BIT(signal);
        return 0;
    }
}

int sig_check(ProcTable *pt, uint32_t pid) {
    Proc8 *p = proc_find(pt, pid);
    if (!p) return -1;

    uint8_t pending = (uint8_t)(p->sig_pending & (uint8_t)~p->sig_mask);
    int handled = 0;
    for (uint8_t sig = 1; sig <= SIG_MAX; sig++) {
        uint8_t bit = SIG_BIT(sig);
        if (!(pending & bit)) continue;
        handled++;
        p->sig_pending &= (uint8_t)~bit;
        switch (sig) {
        case SIG_STOP:
            p->state = PROC_SLEEPING;
            gate_close_space_ctx(pt->ctx, p->space);
            break;
        case SIG_CONT:
            if (p->state == PROC_SLEEPING || p->state == PROC_BLOCKED) {
                p->state = PROC_RUNNING;
                gate_open_space_ctx(pt->ctx, p->space);
            }
            break;
        case SIG_SEGV:
            (void)proc_exit(pt, pid, 139);
            break;
        case SIG_CHILD:
        case SIG_USR1:
        case SIG_USR2:
        case SIG_ALARM:
        default:
            break;
        }
    }
    return handled;
}

void sig_mask_set(ProcTable *pt, uint32_t pid, uint8_t mask) {
    Proc8 *p = proc_find(pt, pid);
    if (p) p->sig_mask |= (uint8_t)(mask & (uint8_t)~SIG_UNMASKABLE);
}

void sig_mask_clear(ProcTable *pt, uint32_t pid, uint8_t mask) {
    Proc8 *p = proc_find(pt, pid);
    if (p) p->sig_mask &= (uint8_t)~mask;
}
