/*
 * path_virtual.c — Phase-10: Virtual Path Layer
 *
 * Provides resolution for special virtual paths:
 *   /proc/<pid>     → Process info (read-only)
 *   /proc/self      → Current process
 *   /dev/null       → Discard sink
 *   /dev/canvas     → Raw canvas access
 *   ~               → User home directory (lane_id-based)
 *   /wh             → WhiteHole log (read-only)
 *   /bh             → BlackHole compression summary (read-only)
 *
 * Virtual FsKey encoding:
 *   gate_id 0xFF00 → /proc namespace (slot = pid)
 *   gate_id 0xFF01 → /dev namespace  (slot = device_id)
 *   gate_id 0xFF02 → /wh namespace   (slot = tick offset)
 *   gate_id 0xFF03 → /bh namespace   (slot = summary_id)
 */
#include "../include/canvasos_path.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_user.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include <string.h>
#include <stdio.h>

/* Virtual namespace gate IDs */
#define VIRT_PROC_GATE   0xFF00u
#define VIRT_DEV_GATE    0xFF01u
#define VIRT_WH_GATE     0xFF02u
#define VIRT_BH_GATE     0xFF03u

/* Device IDs */
#define DEV_NULL    0
#define DEV_CANVAS  1

/* ── Resolve virtual paths ───────────────────────────── */
int path_resolve_virtual(EngineContext *ctx, PathContext *pc,
                         const char *path, FsKey *out) {
    if (!pc || !path || !out) return -1;
    (void)ctx;

    /* Root */
    if (strcmp(path, "/") == 0) {
        *out = pc->root;
        return 0;
    }

    /* Home directory (~) */
    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/')) {
        /* Home = tile offset based on lane/uid.
         * Convention: home gate_id = lane_id * 16 + 1 */
        uint16_t home_gate = (uint16_t)((pc->pid & 0xFF) * 16 + 1);
        out->gate_id = home_gate;
        out->slot = 0;
        /* If there's a subpath after ~, we pass to normal resolution */
        if (path[1] == '/') return -1; /* Needs further resolution */
        return 0;
    }

    /* /proc namespace */
    if (strncmp(path, "/proc", 5) == 0) {
        if (path[5] == '\0') {
            /* /proc itself */
            out->gate_id = VIRT_PROC_GATE;
            out->slot = 0xFF; /* Directory marker */
            return 0;
        }
        if (path[5] == '/') {
            const char *rest = path + 6;

            /* /proc/self */
            if (strcmp(rest, "self") == 0) {
                out->gate_id = VIRT_PROC_GATE;
                out->slot = (uint8_t)(pc->pid & 0xFF);
                return 0;
            }

            /* /proc/<pid> */
            unsigned pid = 0;
            if (sscanf(rest, "%u", &pid) == 1 && pid < 256) {
                out->gate_id = VIRT_PROC_GATE;
                out->slot = (uint8_t)pid;
                return 0;
            }
        }
        return -1;
    }

    /* /dev namespace */
    if (strncmp(path, "/dev", 4) == 0) {
        if (path[4] == '\0') {
            out->gate_id = VIRT_DEV_GATE;
            out->slot = 0xFF; /* Directory marker */
            return 0;
        }
        if (path[4] == '/') {
            const char *dev = path + 5;

            if (strcmp(dev, "null") == 0) {
                out->gate_id = VIRT_DEV_GATE;
                out->slot = DEV_NULL;
                return 0;
            }
            if (strcmp(dev, "canvas") == 0) {
                out->gate_id = VIRT_DEV_GATE;
                out->slot = DEV_CANVAS;
                return 0;
            }
        }
        return -1;
    }

    /* /wh namespace (WhiteHole log) */
    if (strncmp(path, "/wh", 3) == 0) {
        if (path[3] == '\0') {
            out->gate_id = VIRT_WH_GATE;
            out->slot = 0;
            return 0;
        }
        if (path[3] == '/') {
            unsigned tick = 0;
            if (sscanf(path + 4, "%u", &tick) == 1) {
                out->gate_id = VIRT_WH_GATE;
                out->slot = (uint8_t)(tick & 0xFF);
                return 0;
            }
        }
        return -1;
    }

    /* /bh namespace (BlackHole) */
    if (strncmp(path, "/bh", 3) == 0) {
        if (path[3] == '\0') {
            out->gate_id = VIRT_BH_GATE;
            out->slot = 0;
            return 0;
        }
        if (path[3] == '/') {
            unsigned id = 0;
            if (sscanf(path + 4, "%u", &id) == 1) {
                out->gate_id = VIRT_BH_GATE;
                out->slot = (uint8_t)(id & 0xFF);
                return 0;
            }
        }
        return -1;
    }

    return -1; /* Not a virtual path */
}

/* ── Render virtual path content ─────────────────────── */
int path_render_virtual(const ProcTable *pt, EngineContext *ctx,
                        FsKey key, char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    buf[0] = '\0';

    /* /proc/<pid> */
    if (key.gate_id == VIRT_PROC_GATE) {
        if (key.slot == 0xFF) {
            /* /proc directory listing */
            int written = 0;
            if (pt) {
                for (int i = 0; i < PROC8_MAX && (size_t)written < cap - 32; i++) {
                    const Proc8 *p = &pt->procs[i];
                    if (p->pid == 0xFFFFFFFF) continue;
                    if (p->state == PROC_ZOMBIE && p->energy == 0 && p->pid != 0)
                        continue;
                    written += snprintf(buf + written, cap - (size_t)written,
                                        "%u\n", p->pid);
                }
            }
            return 0;
        }

        /* /proc/<pid> — single process info */
        if (!pt) return -1;
        const Proc8 *p = proc_find((ProcTable *)pt, key.slot);
        if (!p) return -1;

        const char *st = (p->state == PROC_RUNNING)  ? "RUNNING" :
                         (p->state == PROC_SLEEPING) ? "SLEEPING" :
                         (p->state == PROC_BLOCKED)  ? "BLOCKED" : "ZOMBIE";
        snprintf(buf, cap,
                 "PID=%u\nSTATE=%s\nPARENT=%u\n"
                 "ENERGY=%u/%u\nLANE=%u\n"
                 "CODE_TILE=%u\nTICK_BORN=%u\n",
                 p->pid, st, p->parent_pid,
                 p->energy, p->energy_max, p->lane_id,
                 p->code_tile, p->tick_born);
        return 0;
    }

    /* /dev/null — always empty */
    if (key.gate_id == VIRT_DEV_GATE && key.slot == DEV_NULL) {
        buf[0] = '\0';
        return 0;
    }

    /* /dev/canvas — canvas info */
    if (key.gate_id == VIRT_DEV_GATE && key.slot == DEV_CANVAS) {
        if (!ctx) return -1;
        uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
        snprintf(buf, cap, "CANVAS %ux%u TICK=%u HASH=%08X\n",
                 CANVAS_W, CANVAS_H, ctx->tick, h);
        return 0;
    }

    /* /wh — WhiteHole log entry */
    if (key.gate_id == VIRT_WH_GATE) {
        if (!ctx) return -1;
        WhRecord r;
        memset(&r, 0, sizeof(r));
        if (wh_read_record(ctx, (uint32_t)key.slot, &r)) {
            snprintf(buf, cap,
                     "TICK=%u OP=0x%02X ADDR=%u KIND=%u P0=%u P1=%u\n",
                     r.tick_or_event, r.opcode_index,
                     r.target_addr, r.target_kind,
                     r.param0, r.param1);
        } else {
            snprintf(buf, cap, "(empty)\n");
        }
        return 0;
    }

    /* /bh — BlackHole summary */
    if (key.gate_id == VIRT_BH_GATE) {
        snprintf(buf, cap, "BH_SUMMARY slot=%u\n", key.slot);
        return 0;
    }

    return -1; /* Unknown virtual key */
}

/* ── Check if a key is virtual ───────────────────────── */
int path_is_virtual(FsKey key) {
    return (key.gate_id >= 0xFF00u) ? 1 : 0;
}

/* ── List virtual directory entries ──────────────────── */
int path_ls_virtual(FsKey dir, char names[][16], FsKey keys[], int max) {
    int count = 0;

    if (dir.gate_id == VIRT_PROC_GATE && dir.slot == 0xFF) {
        /* /proc listing — we just show placeholder entries */
        if (count < max) {
            strncpy(names[count], "self", 16);
            keys[count] = (FsKey){VIRT_PROC_GATE, 0};
            count++;
        }
        return count;
    }

    if (dir.gate_id == VIRT_DEV_GATE && dir.slot == 0xFF) {
        if (count < max) {
            strncpy(names[count], "null", 16);
            keys[count] = (FsKey){VIRT_DEV_GATE, DEV_NULL};
            count++;
        }
        if (count < max) {
            strncpy(names[count], "canvas", 16);
            keys[count] = (FsKey){VIRT_DEV_GATE, DEV_CANVAS};
            count++;
        }
        return count;
    }

    return count;
}
