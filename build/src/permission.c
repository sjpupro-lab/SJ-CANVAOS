/* permission.c — Phase-11 Sprint 5: Permission Verification */
#include "../include/canvasos_permission.h"

bool perm_check(uint16_t uid, uint16_t gid,
                const FilePermission *fp, uint8_t requested)
{
    if (!fp) return false;
    if (perm_is_root(uid)) return true;

    uint8_t granted;
    if (uid == fp->uid)
        granted = (uint8_t)((fp->mode >> 6) & 0x07);
    else if (gid == fp->gid)
        granted = (uint8_t)((fp->mode >> 3) & 0x07);
    else
        granted = (uint8_t)(fp->mode & 0x07);

    return (granted & requested) == requested;
}

bool gate_perm_check(uint16_t uid, const GatePermission *gp, uint8_t requested)
{
    if (!gp) return false;
    if (perm_is_root(uid)) return true;
    if (uid != gp->owner_uid) return false;
    return (gp->perm_bits & requested) == requested;
}
