#pragma once
/* canvasos_user.h — Phase-8: User/Permission (Lane = User) */
#include <stdint.h>
#include "canvasos_types.h"
#include "canvasos_mprotect.h"

#define PRIV_ROOT   0x80
#define PRIV_USER   0x40
#define PRIV_GUEST  0x20

#define UID_ROOT    0     /* Lane 0 = root */
#define UID_MAX     255

typedef struct {
    uint8_t   lane_id;     /* = uid */
    char      name[16];
    uint16_t  home_tile;
    uint8_t   priv;
    bool      active;
} User;

#define USER_MAX  32

typedef struct {
    User users[USER_MAX];
    uint32_t count;
} UserTable;

void usertable_init(UserTable *ut);
int  user_create(UserTable *ut, uint8_t lane_id, const char *name, uint8_t priv);
int  user_check_perm(const UserTable *ut, const TileProtection *tp,
                     uint8_t uid, uint16_t tile_id, uint8_t perm);
int  user_su(UserTable *ut, void *pt, uint32_t pid, uint8_t target_lane);
