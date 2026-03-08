/*
 * user.c — Phase-10: User/Permission (Lane = User)
 */
#include "../include/canvasos_user.h"
#include "../include/canvasos_proc.h"
#include <string.h>
#include <stdio.h>

void usertable_init(UserTable *ut) {
    memset(ut, 0, sizeof(*ut));

    /* root 자동 생성 */
    User *root    = &ut->users[0];
    root->lane_id = UID_ROOT;
    strncpy(root->name, "root", sizeof(root->name));
    root->home_tile = 0;
    root->priv    = PRIV_ROOT;
    root->active  = true;
    ut->count     = 1;
}

int user_create(UserTable *ut, uint8_t lane_id, const char *name, uint8_t priv) {
    if (!ut || !name || ut->count >= USER_MAX) return -1;

    /* 중복 체크 */
    for (uint32_t i = 0; i < ut->count; i++)
        if (ut->users[i].active && ut->users[i].lane_id == lane_id) return -2;

    User *u = &ut->users[ut->count];
    u->lane_id   = lane_id;
    strncpy(u->name, name, sizeof(u->name) - 1);
    u->home_tile = (uint16_t)(lane_id * 16); /* 간이: lane*16 = 홈 타일 */
    u->priv      = priv;
    u->active    = true;
    ut->count++;
    return 0;
}

int user_check_perm(const UserTable *ut, const TileProtection *tp,
                    uint8_t uid, uint16_t tile_id, uint8_t perm) {
    if (!ut || !tp) return -1;
    if (uid == UID_ROOT) return 0; /* root = 전능 */

    return tile_check(tp, uid, tile_id, perm);
}

int user_su(UserTable *ut, void *pt_v, uint32_t pid, uint8_t target_lane) {
    if (!ut || !pt_v) return -1;
    ProcTable *pt = (ProcTable *)pt_v;

    Proc8 *p = proc_find(pt, pid);
    if (!p) return -1;

    /* root만 su 가능 */
    if (p->lane_id != UID_ROOT) return -2;

    /* target lane 존재 확인 */
    bool found = false;
    for (uint32_t i = 0; i < ut->count; i++)
        if (ut->users[i].active && ut->users[i].lane_id == target_lane)
            { found = true; break; }
    if (!found) return -3;

    p->lane_id = target_lane;
    return 0;
}
