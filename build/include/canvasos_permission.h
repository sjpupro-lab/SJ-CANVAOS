#pragma once
/* canvasos_permission.h — Phase-11 Sprint 5: uid/gid Permission Model */
#include <stdint.h>
#include <stdbool.h>

#define PERM_READ   0x04
#define PERM_WRITE  0x02
#define PERM_EXEC   0x01

#define PERM_MODE_FILE  ((PERM_READ|PERM_WRITE)<<6 | PERM_READ<<3 | PERM_READ)
#define PERM_MODE_DIR   ((PERM_READ|PERM_WRITE|PERM_EXEC)<<6 | \
                         (PERM_READ|PERM_EXEC)<<3 | (PERM_READ|PERM_EXEC))

#define UID_ROOT  0

typedef struct {
    uint16_t uid;
    uint16_t gid;
    uint16_t mode;   /* rwxrwxrwx (9-bit) */
} FilePermission;

#define GATE_PERM_READ    0x01
#define GATE_PERM_WRITE   0x02
#define GATE_PERM_EXEC    0x04
#define GATE_PERM_SHARED  0x08

typedef struct {
    uint16_t owner_uid;
    uint8_t  perm_bits;
} GatePermission;

bool perm_check(uint16_t uid, uint16_t gid,
                const FilePermission *fp, uint8_t requested);

static inline bool perm_is_root(uint16_t uid) {
    return uid == UID_ROOT;
}

bool gate_perm_check(uint16_t uid, const GatePermission *gp, uint8_t requested);
