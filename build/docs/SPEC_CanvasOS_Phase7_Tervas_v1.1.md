# CanvasOS Phase-7 Tervas — 표준 명세 v1.1

> **릴리스**: v1.0.1-p7  
> **상태**: 정식 통합 완료  
> **기반**: Phase-6 결정론 엔진 (DK-1/2/3 유지)  
> **변경**: v1.0 → v1.1 — Cell 구조, Canvas 크기, Projection 알고리즘, tick window 추가

---

## 0. 불변 규약 (IMMUTABLE — 절대 수정 불가)

> 이 섹션은 코드보다 우선한다.  
> 이후 어떤 AI, 어떤 개발자가 건드리더라도 아래 규칙은 파기할 수 없다.

| ID | 규칙 | 위반 시 |
|----|------|---------|
| **R-1** | Y축 = 시간축. Y↑ = 과거, Y↓ = 미래. 방향 역전 금지 | replay 전체 파손 |
| **R-2** | 4분면 고정: Q0(+x,+y) Q1(−x,+y) Q2(−x,−y) Q3(+x,−y). 재배치 금지 | WH/BH 영역 파손 |
| **R-3** | 레이어 시스템 없음. Projection(조건 필터)만 허용 | 엔진 결정론 파손 |
| **R-4** | Tervas는 READ-ONLY. 엔진 상태를 절대 수정하지 않는다 | canvas hash 오염 |
| **R-5** | Snapshot은 tick boundary 이후에만 캡처 (merge_tick 완료 후) | DK-1 위반 |
| **R-6** | Projection 연산은 정수 연산만 (float 금지, DK-2 상속) | 결정론 파손 |

---

## 1. 목적

Phase-6 결정론 엔진 위에 다음 기능을 제공하는 **읽기 전용 운영 인터페이스**를 추가한다.

- Canvas 상태 시각화 (ASCII → NCurses → SDL2 → OpenGL 업그레이드 경로 정의)
- A/B 값 집합 기반 Projection
- WH/BH 영역 확인
- tick 스냅샷 탐색
- CLI 기반 엔진 제어

---

## 2. 핵심 데이터 구조 정의

### 2.1 Canvas 크기

```c
#define CANVAS_WIDTH   1024     /* x축 셀 수 */
#define CANVAS_HEIGHT  1024     /* y축 셀 수 */
/* 전체 셀 수: 1,048,576 (1024 × 1024) */
/* 메모리 크기: 8MB (셀 1개 = 8 bytes) */
```

### 2.2 Cell 구조체

```c
typedef struct {
    uint32_t A;   /* Spatial Address Layer — Where (공간 주소 / LaneID 상위 8비트) */
    uint8_t  B;   /* Behavior Layer       — opcode / bpage index                  */
    uint8_t  G;   /* State Layer          — flags / energy (0 = inactive)         */
    uint8_t  R;   /* Stream Layer         — payload byte                          */
    uint8_t  pad; /* reserved, v1 = 0                                             */
} Cell;
/* sizeof(Cell) == 8 bytes */
```

**채널별 역할 요약**

| 채널 | 비트 | Tervas에서의 의미 |
|------|------|------------------|
| A | 32bit | Projection 필터 기준값. LaneID = A[31:24] |
| B | 8bit | opcode. Projection 필터 기준값 |
| G | 8bit | 에너지. G=0 → inactive (렌더러에서 dim 처리) |
| R | 8bit | 페이로드 바이트. ASCII 문자로 직접 표시 가능 |

### 2.3 WH/BH 영역 좌표 (불변)

```c
/* 위치 기준: Q3 영역 (x≥512, y≥512) */
#define WH_X0   512     /* WhiteHole 시작 x */
#define WH_Y0   512     /* WhiteHole 시작 y */
#define WH_W    512     /* 폭 */
#define WH_H    128     /* 높이 = WH_TILES_Y(8) × TILE_SZ(16) */

#define BH_X0   512     /* BlackHole 시작 x */
#define BH_Y0   640     /* BlackHole 시작 y = WH_Y0 + WH_H */
#define BH_W    512     /* 폭 */
#define BH_H    64      /* 높이 = BH_TILES_Y(4) × TILE_SZ(16) */

/* 불변 조건: WH ∩ BH = ∅ (항상) */
```

### 2.4 tick Window 정의

```c
/* WH ring 용량 (replay 가능한 최대 tick 범위) */
#define WH_RECS_PER_ROW  256          /* = WH_W(512) / WH_RECORD_CELLS(2) */
#define WH_CAP           32768        /* = WH_RECS_PER_ROW × WH_H(128) */

/* Tervas tick goto 클램프 범위 */
/* 현재 tick = T 일 때:                        */
/*   lo  = T > WH_CAP ? T - WH_CAP : 0         */
/*   허용 범위: [lo, T]                         */
/*   범위 초과 → 자동 클램프 + 경고 메시지      */
```

---

## 3. 시스템 구조

```
CanvasOS Engine (Phase-6)
   │  tick boundary 이후에만 (R-5)
   ▼
tervas_bridge         ← READ-ONLY adapter (R-4)
   │
   ├── TV_SNAP_FULL      (8MB full copy)
   ├── TV_SNAP_WINDOW    (viewport crop, << 8MB)
   └── TV_SNAP_COMPACT   (active cells only, sparse)
   │
   ▼
Tervas Core
   │
   ├── Projection Engine   (tv_cell_visible, 정수 연산 — R-6)
   ├── CLI Parser          (tv_cli_exec, full validation)
   └── Renderer Backend    (ASCII → NCurses → SDL2 → OpenGL)
```

---

## 4. Projection 명세

### 4.1 모드 정의

| 모드 | 조건 | 설명 |
|------|------|------|
| `TV_PROJ_ALL` | 항상 true | 전체 canvas |
| `TV_PROJ_A` | `cell.A ∈ a_values[]` | A 값 집합 필터 |
| `TV_PROJ_B` | `cell.B ∈ b_values[]` | B 값 집합 필터 |
| `TV_PROJ_AB_UNION` | A OR B | A ∪ B |
| `TV_PROJ_AB_OVERLAP` | A AND B | A ∩ B |
| `TV_PROJ_WH` | (x,y) ∈ WH 영역 | x∈[512,1023] y∈[512,639] |
| `TV_PROJ_BH` | (x,y) ∈ BH 영역 | x∈[512,1023] y∈[640,703] |

### 4.2 Projection 알고리즘 (의사코드)

```c
/*
 * tv_cell_visible — Projection 핵심 함수
 *
 * 규약:
 *   [R-3] 레이어 없음 — canvas 원본에서 조건 필터만
 *   [R-6] 정수 연산만 — float, 나눗셈 금지
 *   결정론: 동일 (x, y, cell, filter) → 항상 동일 결과
 */
bool tv_cell_visible(uint32_t x, uint32_t y,
                     const Cell *c, const TvFilter *f,
                     const uint8_t *gates)
{
    switch (f->mode) {
    case TV_PROJ_ALL:
        return true;

    case TV_PROJ_A:
        return match_A(c->A, f->a_values, f->a_count);

    case TV_PROJ_B:
        return match_B(c->B, f->b_values, f->b_count);

    case TV_PROJ_AB_UNION:
        return match_A(c->A, f->a_values, f->a_count)
            || match_B(c->B, f->b_values, f->b_count);

    case TV_PROJ_AB_OVERLAP:
        return match_A(c->A, f->a_values, f->a_count)
            && match_B(c->B, f->b_values, f->b_count);

    case TV_PROJ_WH:
        return (x >= WH_X0 && x < WH_X0 + WH_W)
            && (y >= WH_Y0 && y < WH_Y0 + WH_H);

    case TV_PROJ_BH:
        return (x >= BH_X0 && x < BH_X0 + BH_W)
            && (y >= BH_Y0 && y < BH_Y0 + BH_H);

    default:
        return true;
    }
}

/* 집합 탐색 — O(n), n ≤ TV_MAX_A/B(64) */
static bool match_A(uint32_t a, const uint32_t *vals, int n) {
    for (int i = 0; i < n; i++)
        if (a == vals[i]) return true;
    return false;
}
static bool match_B(uint8_t b, const uint8_t *vals, int n) {
    for (int i = 0; i < n; i++)
        if (b == vals[i]) return true;
    return false;
}
```

**성능 특성**

```
TV_PROJ_ALL         : O(1)  — 분기 없음
TV_PROJ_A/B         : O(n)  — n ≤ 64 → 최대 64회 비교
TV_PROJ_AB_UNION    : O(n)  — short-circuit OR
TV_PROJ_AB_OVERLAP  : O(n)  — short-circuit AND
TV_PROJ_WH/BH       : O(1)  — 범위 비교 4회
```

### 4.3 집합 제한

```c
#define TV_MAX_A  64    /* A 집합 최대 원소 수 */
#define TV_MAX_B  64    /* B 집합 최대 원소 수 */
/* 초과 시: TV_ERR_OVERFLOW 반환 (silent failure 없음) */
```

---

## 5. Snapshot 전략 — 비용 분석

| 모드 | 복사 대상 | 비용 | 용도 |
|------|-----------|------|------|
| `TV_SNAP_FULL` | 전체 1024×1024 canvas | **8 MB / tick** | 정확성 최우선, 기본값 |
| `TV_SNAP_WINDOW` | viewport.w × viewport.h만 | **O(viewport)** | 실시간 렌더링 |
| `TV_SNAP_COMPACT` | G>0 또는 B≠0 셀만 | **O(active count)** | 희소 캔버스 분석 |

**미래 병목 대비**

```
해상도 2048×2048  → full copy = 32MB    → TV_SNAP_WINDOW 기본값 전환
틱 속도 1000Hz   → compact 또는 diff-only 전략 필요
```

업그레이드 경로: `FULL` → `WINDOW` → diff-only (Δ 누적) → mmap+CoW

---

## 6. CLI 명령 명세

### 6.1 전체 명령 목록

| 명령 | 구문 | 오류 조건 |
|------|------|-----------|
| `view all` | — | — |
| `view a` | `view a <hex> [hex...]` | 값 없음, 초과(>64) → `TV_ERR_OVERFLOW` |
| `view b` | `view b <hex> [hex...]` | 값 >0xFF, 초과 → `TV_ERR_OVERFLOW` |
| `view ab-union` | — | — |
| `view ab-overlap` | — | — |
| `view wh` | — | — |
| `view bh` | — | — |
| `inspect` | `inspect <x> <y>` | x≥1024 or y≥1024 → `TV_ERR_OOB` |
| `tick now` | — | — |
| `tick goto` | `tick goto <n>` | WH 윈도우[T−32768, T] 외 → 클램프+경고 |
| `region` | `region <name>` | 미정의 이름 → `TV_ERR_NO_REGION` |
| `snap` | `snap <full\|win\|compact>` | 알 수 없는 모드 → `TV_ERR_CMD` |
| `zoom` | `zoom <1‥8>` | 범위 외 → `TV_ERR_ZOOM` |
| `pan` | `pan <x> <y>` | OOB → `TV_ERR_OOB` |
| `refresh` | — | — |
| `help` / `?` | — | — |
| `quit` / `q` / `exit` | — | — |

### 6.2 지원 region 이름

| 이름 | 영역 | 좌표 (x0,y0,w,h) |
|------|------|-------------------|
| `wh` | WhiteHole | 512, 512, 512, 128 |
| `bh` | BlackHole | 512, 640, 512, 64 |
| `cr` | Control Region | 512, 512, 64, 64 |
| `q0` | Quadrant 0 | 0, 0, 512, 512 |
| `q1` | Quadrant 1 | 512, 0, 512, 512 |
| `q2` | Quadrant 2 | 0, 512, 512, 512 |
| `q3` | Quadrant 3 | 512, 512, 512, 512 |
| `full` | 전체 canvas | 0, 0, 1024, 1024 |

### 6.3 오류 코드 체계

```c
TV_OK            =  0
TV_ERR_NULL      = -1   /* NULL 포인터 */
TV_ERR_OOB       = -2   /* 좌표 범위 외 */
TV_ERR_NO_REGION = -3   /* region 미정의 */
TV_ERR_TICK_OOB  = -4   /* tick WH 윈도우 외 (클램프 후 TV_OK 복귀) */
TV_ERR_OVERFLOW  = -5   /* A/B 집합 한도 초과 */
TV_ERR_ZOOM      = -6   /* zoom 범위 외 */
TV_ERR_ALLOC     = -7   /* 메모리 할당 실패 */
TV_ERR_CMD       = -8   /* 알 수 없는 명령 */
```

---

## 7. 렌더러 업그레이드 경로

### 교체 원칙

```c
/* 렌더러 교체 = TvRendererBackend 태그 1개 변경 */
/* Projection / CLI / Engine은 절대 건드리지 않는다 */
tv.renderer_backend = TV_RENDER_ASCII;    /* 현재: 검증용 */
tv.renderer_backend = TV_RENDER_NCURSES;  /* 다음: 운영 UI */
tv.renderer_backend = TV_RENDER_SDL2;     /* 이후: 데스크톱 */
tv.renderer_backend = TV_RENDER_OPENGL;   /* 미래: 셰이더 기반 */
```

### 단계별 특성

| 단계 | 해상도 | 마우스 | 색상 | 상태 |
|------|--------|--------|------|------|
| ASCII | 64×32 문자 | 없음 | ANSI 16색 | ✅ 구현 완료 — CI/검증 전용 |
| NCurses | 터미널 전체 | xterm 마우스 | 256색/truecolor | 권장 다음 단계 |
| SDL2 | 1:1 픽셀 (1024×1024) | 완전 지원 | 32bit RGBA | 데스크톱 전용 |
| OpenGL | GPU 가속 | — | Cell → fragment shader | 미래 |

**NCurses의 핵심 이점**: 마우스 클릭 좌표 → `inspect` 자동 전달, halfdelay 실시간 re-render

**SDL2의 핵심 이점**: Cell.R/G/B → RGBA 직접 매핑, A/B 집합 오버레이 컬러

---

## 8. 파일 구조

```
include/tervas/
  tervas_core.h          핵심 타입, 불변 규약, 렌더러 태그, TvError
  tervas_bridge.h        엔진 어댑터 인터페이스
  tervas_cli.h           CLI 실행 인터페이스
  tervas_projection.h    tv_cell_visible, WH/BH 판별
  tervas_render.h        렌더러 인터페이스

src/tervas/
  tervas_core.c          init/free (heap canvas)
  tervas_bridge.c        snapshot 3모드, inspect, region
  tervas_cli.c           전체 명령 파서 + 입력 검증
  tervas_projection.c    Projection 연산 (정수만)
  tervas_render_ascii.c  ASCII 렌더러 (검증용)
  tervas_main.c          독립 실행 바이너리

tests/
  test_tervas.c          TV1~TV7 (7개 테스트)
```

---

## 9. 검증 기준 (Phase-7 완료 조건)

| 항목 | 결과 |
|------|------|
| `make tests/test_phase6` | PASS: 6 / FAIL: 0 ✅ |
| `make tests/test_tervas` | PASS: 7 / FAIL: 0 ✅ |
| `make test_all` | Phase-6 + Phase-7 통과 ✅ |
| 엔진 canvas hash 불변 (TV7) | ✅ |
| OOB/bad input 오류 코드 반환 (TV6) | ✅ |
| 결정론 (동일 snapshot → 동일 projection count, TV3) | ✅ |
| 윈도우 스냅샷 (O(viewport) cost, TV2) | ✅ |
| AddressSanitizer (ASAN) | 누수 없음 ✅ |

---

## 10. 다음 Phase 권장 작업

| 우선순위 | 작업 | 근거 |
|----------|------|------|
| 1 | NCurses 렌더러 | 실제 운영 UI. 마우스 → inspect 연결 |
| 2 | diff-only snapshot | 성능 확장 핵심. Δ 누적으로 8MB 복사 제거 |
| 3 | tick history explorer | WH ring 전체를 Tervas에서 슬라이드 — CanvasOS 고유 기능 |
| 4 | region 동적 등록 | `region_register()` 연동, 런타임 영역 정의 |

---

*CanvasOS Phase-7 Tervas Specification v1.1*  
*이 문서는 코드와 함께 버전 관리되어야 한다.*  
*Section 0 불변 규약은 어떤 수정도 허용하지 않는다.*
