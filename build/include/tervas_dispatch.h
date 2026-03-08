#ifndef TERVAS_DISPATCH_H
#include <string.h>
#define TERVAS_DISPATCH_H
/*
 * tervas_dispatch.h — 명령 디스패치 테이블 (Spec §8)
 *
 * 각 명령은 다음 4층을 가진다:
 *   CLI → Bridge → Projection → Renderer
 *
 * 대응표와 코드가 일치해야 한다 (P-10)
 */
#include <stdint.h>
#include "tervas_core.h"

/* ── Bridge 함수 태그 ──────────────────────────────────────────── */
typedef enum {
    TV_BRIDGE_NONE     = 0,
    TV_BRIDGE_SNAPSHOT = 1,   /* tervas_bridge_snapshot() */
    TV_BRIDGE_INSPECT  = 2,   /* tervas_bridge_inspect()  */
    TV_BRIDGE_REGION   = 3,   /* tervas_bridge_region()   */
} TvBridgeOp;

/* ── 렌더러 영향 태그 ──────────────────────────────────────────── */
typedef enum {
    TV_RENDER_EFFECT_NONE       = 0,
    TV_RENDER_EFFECT_FULL       = 1,   /* 전체 갱신                   */
    TV_RENDER_EFFECT_STATUS     = 2,   /* 상태줄만 갱신               */
    TV_RENDER_EFFECT_VIEWPORT   = 3,   /* viewport 이동/scale         */
    TV_RENDER_EFFECT_HELP       = 4,   /* 도움말 출력                 */
    TV_RENDER_EFFECT_QUIT       = 5,   /* 루프 종료                   */
} TvRenderEffect;

/* ── 엔진 읽기 범위 태그 ──────────────────────────────────────── */
typedef enum {
    TV_ENG_READ_NONE      = 0,
    TV_ENG_READ_FULL      = 1,   /* full canvas             */
    TV_ENG_READ_SINGLE    = 2,   /* single cell             */
    TV_ENG_READ_REGION    = 3,   /* region bounds           */
    TV_ENG_READ_TICK      = 4,   /* tick view               */
    TV_ENG_READ_RENDERER  = 5,   /* renderer only (no eng)  */
} TvEngReadScope;

/* ── 비용 분류 ──────────────────────────────────────────────────── */
typedef enum {
    TV_COST_O1         = 0,   /* O(1)                          */
    TV_COST_FULL_SCAN  = 1,   /* O(canvas) = O(1M)             */
    TV_COST_REGION     = 2,   /* O(region area)                */
    TV_COST_MATCH      = 3,   /* O(canvas × set) ≤ O(64M)      */
    TV_COST_REGISTRY   = 4,   /* O(region count)               */
} TvCostClass;

/* ── 단일 명령 디스패치 레코드 ──────────────────────────────────── */
typedef struct {
    const char      *prefix;        /* 명령 접두사 (strcmp/strncmp) */
    TvBridgeOp       bridge;
    TvProjectionMode proj;          /* TV_PROJ_ALL = "유지"        */
    TvRenderEffect   render_effect;
    TvEngReadScope   eng_scope;
    TvError          on_error;      /* 대표 오류 코드              */
    TvCostClass      cost;
    const char      *desc;
} TvCmdDispatch;

/* ── 디스패치 테이블 (Spec §8.1) ─────────────────────────────── */
static const TvCmdDispatch TV_DISPATCH_TABLE[] = {
/* prefix              bridge                 proj                  render_effect          eng_scope            on_error          cost              desc */
{"view all",    TV_BRIDGE_SNAPSHOT, TV_PROJ_ALL,        TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_OK,            TV_COST_FULL_SCAN,"전체 canvas projection"          },
{"view a ",     TV_BRIDGE_SNAPSHOT, TV_PROJ_A,          TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_ERR_OVERFLOW,  TV_COST_MATCH,   "A 값 집합 필터"                  },
{"view b ",     TV_BRIDGE_SNAPSHOT, TV_PROJ_B,          TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_ERR_OVERFLOW,  TV_COST_MATCH,   "B 값 집합 필터"                  },
{"view ab-union",TV_BRIDGE_SNAPSHOT,TV_PROJ_AB_UNION,   TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_OK,            TV_COST_MATCH,   "A∪B union"                       },
{"view ab-overlap",TV_BRIDGE_SNAPSHOT,TV_PROJ_AB_OVERLAP,TV_RENDER_EFFECT_FULL,   TV_ENG_READ_FULL,    TV_OK,            TV_COST_MATCH,   "A∩B overlap"                     },
{"view wh",     TV_BRIDGE_SNAPSHOT, TV_PROJ_WH,         TV_RENDER_EFFECT_FULL,    TV_ENG_READ_REGION,  TV_OK,            TV_COST_REGION,  "WH 영역 강조"                    },
{"view bh",     TV_BRIDGE_SNAPSHOT, TV_PROJ_BH,         TV_RENDER_EFFECT_FULL,    TV_ENG_READ_REGION,  TV_OK,            TV_COST_REGION,  "BH 영역 강조"                    },
{"inspect ",    TV_BRIDGE_INSPECT,  TV_PROJ_ALL,        TV_RENDER_EFFECT_STATUS,  TV_ENG_READ_SINGLE,  TV_ERR_OOB,       TV_COST_O1,      "단일 셀 상세 조회"               },
{"tick now",    TV_BRIDGE_SNAPSHOT, TV_PROJ_ALL,        TV_RENDER_EFFECT_FULL,    TV_ENG_READ_TICK,    TV_OK,            TV_COST_O1,      "현재 tick 조회"                  },
{"tick goto ",  TV_BRIDGE_SNAPSHOT, TV_PROJ_ALL,        TV_RENDER_EFFECT_FULL,    TV_ENG_READ_TICK,    TV_ERR_TICK_OOB,  TV_COST_O1,      "tick 스냅샷 이동 (clamp)"        },
{"region ",     TV_BRIDGE_REGION,   TV_PROJ_ALL,        TV_RENDER_EFFECT_VIEWPORT,TV_ENG_READ_REGION,  TV_ERR_NO_REGION, TV_COST_REGISTRY,"viewport 이동/영역 강조"         },
{"snap ",       TV_BRIDGE_NONE,     TV_PROJ_ALL,        TV_RENDER_EFFECT_NONE,    TV_ENG_READ_NONE,    TV_ERR_CMD,       TV_COST_O1,      "스냅샷 모드 전환"                },
{"zoom ",       TV_BRIDGE_NONE,     TV_PROJ_ALL,        TV_RENDER_EFFECT_VIEWPORT,TV_ENG_READ_RENDERER,TV_ERR_ZOOM,      TV_COST_O1,      "viewport scale 변경"             },
{"pan ",        TV_BRIDGE_NONE,     TV_PROJ_ALL,        TV_RENDER_EFFECT_VIEWPORT,TV_ENG_READ_RENDERER,TV_ERR_OOB,       TV_COST_O1,      "viewport 이동"                   },
{"refresh",     TV_BRIDGE_SNAPSHOT, TV_PROJ_ALL,        TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_OK,            TV_COST_FULL_SCAN,"현재 mode 재계산"                },
{"help",        TV_BRIDGE_NONE,     TV_PROJ_ALL,        TV_RENDER_EFFECT_HELP,    TV_ENG_READ_NONE,    TV_OK,            TV_COST_O1,      "도움말"                          },
{"quit",        TV_BRIDGE_NONE,     TV_PROJ_ALL,        TV_RENDER_EFFECT_QUIT,    TV_ENG_READ_NONE,    TV_OK,            TV_COST_O1,      "종료"                            },
/* quick 명령군 (Spec §12) — 표준 명령의 별칭/조합 */
{"quick wh",    TV_BRIDGE_SNAPSHOT, TV_PROJ_WH,         TV_RENDER_EFFECT_FULL,    TV_ENG_READ_REGION,  TV_OK,            TV_COST_REGION,  "≡ view wh"                       },
{"quick bh",    TV_BRIDGE_SNAPSHOT, TV_PROJ_BH,         TV_RENDER_EFFECT_FULL,    TV_ENG_READ_REGION,  TV_OK,            TV_COST_REGION,  "≡ view bh"                       },
{"quick all",   TV_BRIDGE_SNAPSHOT, TV_PROJ_ALL,        TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_OK,            TV_COST_FULL_SCAN,"≡ view all"                      },
{"quick overlap",TV_BRIDGE_SNAPSHOT,TV_PROJ_AB_OVERLAP, TV_RENDER_EFFECT_FULL,    TV_ENG_READ_FULL,    TV_OK,            TV_COST_MATCH,   "≡ view ab-overlap"               },
{"quick now",   TV_BRIDGE_SNAPSHOT, TV_PROJ_ALL,        TV_RENDER_EFFECT_FULL,    TV_ENG_READ_TICK,    TV_OK,            TV_COST_O1,      "≡ tick now + refresh"            },
{"quick inspect ",TV_BRIDGE_INSPECT,TV_PROJ_ALL,        TV_RENDER_EFFECT_STATUS,  TV_ENG_READ_SINGLE,  TV_ERR_OOB,       TV_COST_O1,      "≡ inspect x y"                   },
{"quick region ",TV_BRIDGE_REGION,  TV_PROJ_ALL,        TV_RENDER_EFFECT_VIEWPORT,TV_ENG_READ_REGION,  TV_ERR_NO_REGION, TV_COST_REGISTRY,"≡ region + focus"                },
{NULL, 0, 0, 0, 0, 0, 0, NULL}
};

#define TV_DISPATCH_COUNT \
    ((int)(sizeof(TV_DISPATCH_TABLE)/sizeof(TV_DISPATCH_TABLE[0])) - 1)

/* 테이블 조회 — NULL이면 미등록 명령 */
static inline const TvCmdDispatch *tv_dispatch_find(const char *cmd) {
    for (int i = 0; TV_DISPATCH_TABLE[i].prefix; i++) {
        size_t plen = __builtin_strlen(TV_DISPATCH_TABLE[i].prefix);
        if (strncmp(cmd, TV_DISPATCH_TABLE[i].prefix, plen) == 0)
            return &TV_DISPATCH_TABLE[i];
    }
    return NULL;
}


#endif /* TERVAS_DISPATCH_H */
