# CanvasOS Phase-7 Tervas — 표준 명세 v1.0

> **릴리스**: v1.0.1-p7  
> **상태**: 정식 통합 완료  
> **기반**: Phase-6 결정론 엔진 (DK-1/2/3 유지)

---

## 0. 불변 규약 (IMMUTABLE — 절대 수정 불가)

> 이 섹션은 코드보다 우선한다.  
> 이후 어떤 AI, 어떤 개발자가 이 코드를 건드리더라도  
> **다음 규칙은 파기할 수 없다.**

| ID | 규칙 | 위반 시 |
|----|------|---------|
| **R-1** | Y축 = 시간축. Y↑ = 과거, Y↓ = 미래. Y축 방향 역전 금지 | 전체 replay 파손 |
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

## 2. 시스템 구조

```
CanvasOS Engine (Phase-6)
   │  tick boundary 이후에만
   ▼
tervas_bridge         ← READ-ONLY adapter
   │
   ├── TV_SNAP_FULL      (8MB full copy)
   ├── TV_SNAP_WINDOW    (viewport crop, << 8MB)
   └── TV_SNAP_COMPACT   (active cells only, sparse)
   │
   ▼
Tervas Core
   │
   ├── Projection Engine   (tv_cell_visible, 정수 연산)
   ├── CLI Parser          (tv_cli_exec, full validation)
   └── Renderer Backend    (ASCII → ncurses → SDL2 → OpenGL)
```

---

## 3. Snapshot 전략 — 비용 분석

### 현재 구현 (v1.0)

| 모드 | 복사 대상 | 비용 | 용도 |
|------|-----------|------|------|
| `TV_SNAP_FULL` | 전체 1024×1024 canvas | **8 MB / tick** | 정확성 최우선, 기본값 |
| `TV_SNAP_WINDOW` | viewport.w × viewport.h만 | **O(viewport)** | 실시간 렌더링 |
| `TV_SNAP_COMPACT` | G>0 또는 B≠0 셀만 | **O(active count)** | 희소 캔버스 분석 |

### 미래 병목 대비 지침

```
해상도 2048×2048 → full copy = 32MB → TV_SNAP_WINDOW 필수
틱 속도 1000Hz  → compact 또는 diff-only 전략 필요
```

**권장 업그레이드 경로**:
1. v1.x: `TV_SNAP_WINDOW` 기본값으로 전환
2. v2.0: diff-only (Δ 누적) — canvas 전체 복사 제거
3. v3.0: mmap + CoW (Copy-on-Write) — 복사 0에 근접

> **현재 규칙**: snap_mode를 지정하지 않으면 `TV_SNAP_FULL`. 정확도 우선.

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

### 4.2 WH/BH 영역 좌표 (불변)

```
WH: x0=512  y0=512  w=512  h=128   (WH_TILES_Y=8 × TILE_SZ=16)
BH: x0=512  y0=640  w=512  h=64    (BH_TILES_Y=4 × TILE_SZ=16)
WH ∩ BH = ∅  (항상)
```

### 4.3 집합 제한

```c
#define TV_MAX_A  64    /* A 집합 최대 원소 수 */
#define TV_MAX_B  64    /* B 집합 최대 원소 수 */
```

초과 시 `TV_ERR_OVERFLOW` 반환. 무시하지 않는다.

---

## 5. CLI 명령 명세 (표준화)

### 5.1 전체 명령 목록

| 명령 | 구문 | 오류 조건 |
|------|------|-----------|
| `view all` | — | 없음 |
| `view a` | `view a <hex> [hex...]` | 값 없음, 초과(>64) → `TV_ERR_OVERFLOW` |
| `view b` | `view b <hex> [hex...]` | 값 >0xFF, 초과 → `TV_ERR_OVERFLOW` |
| `view ab-union` | — | 없음 |
| `view ab-overlap` | — | 없음 |
| `view wh` | — | 없음 |
| `view bh` | — | 없음 |
| `inspect` | `inspect <x> <y>` | OOB (x≥1024 or y≥1024) → `TV_ERR_OOB` |
| `tick now` | — | 없음 |
| `tick goto` | `tick goto <n>` | WH 윈도우 외 → 자동 클램프 + 경고 |
| `region` | `region <name>` | 미존재 → `TV_ERR_NO_REGION` |
| `snap` | `snap <full\|win\|compact>` | 알 수 없는 모드 → `TV_ERR_CMD` |
| `zoom` | `zoom <1-8>` | 범위 외 → `TV_ERR_ZOOM` |
| `pan` | `pan <x> <y>` | OOB → `TV_ERR_OOB` |
| `refresh` | — | 없음 |
| `help` / `?` | — | 없음 |
| `quit` / `q` / `exit` | — | 없음 |

### 5.2 지원 region 이름

```
wh    BH    cr    q0    q1    q2    q3    full
```

### 5.3 오류 코드 체계

```c
TV_OK            =  0
TV_ERR_NULL      = -1   /* NULL 포인터 */
TV_ERR_OOB       = -2   /* 좌표 범위 외 */
TV_ERR_NO_REGION = -3   /* region 미정의 */
TV_ERR_TICK_OOB  = -4   /* tick WH 윈도우 외 (클램프 후 TV_OK) */
TV_ERR_OVERFLOW  = -5   /* A/B 집합 한도 초과 */
TV_ERR_ZOOM      = -6   /* zoom 범위 외 */
TV_ERR_ALLOC     = -7   /* 메모리 할당 실패 */
TV_ERR_CMD       = -8   /* 알 수 없는 명령 */
```

---

## 6. 렌더러 업그레이드 경로

### 6.1 현재: ASCII (v1.0) — 검증용

```
해상도: 64×32 문자 (1cell = 16×32 canvas cells)
마우스: 없음
색상: ANSI 16색
용도: CI 검증, 서버 headless, 초기 운영
```

### 6.2 업그레이드 1: NCurses (권장 다음 단계)

```
해상도: 터미널 전체 (80×24 ~ 256×64)
마우스: xterm 마우스 프로토콜
색상: 256색 / truecolor
핵심 이점: 마우스 클릭 → inspect 좌표 자동 전달
           실시간 re-render (halfdelay 모드)
구현: tv_render_ncurses.c 추가, TvRendererBackend 전환만으로 교체
```

### 6.3 업그레이드 2: SDL2 (데스크톱 전용)

```
해상도: 1:1 픽셀 매핑 가능 (1024×1024 윈도우)
마우스: 완전 지원 (드래그 선택, 좌표 표시)
색상: 32bit RGBA (Cell.R/G/B/A 직접 매핑)
스냅샷: TV_SNAP_WINDOW + GPU texture upload
핵심 이점: A/B 집합 비교를 색상 레이어로 오버레이
```

### 6.4 업그레이드 3: OpenGL/WebGL (미래)

```
Cell → fragment shader: A=position, B=opcode, G=energy, R=payload
에너지 히트맵, lane 흐름 시각화, 시간축 스크롤
```

### 6.5 교체 원칙

```c
/* 렌더러는 TvRendererBackend 태그만 바꾸면 교체 완료 */
tv.renderer_backend = TV_RENDER_NCURSES;  /* ASCII → NCurses */
tv.renderer_backend = TV_RENDER_SDL2;     /* NCurses → SDL2  */
/* 엔진 코드, Projection, CLI는 전혀 건드리지 않는다 */
```

---

## 7. 파일 구조

```
include/tervas/
  tervas_core.h       — 핵심 타입, 불변 규약 주석, 렌더러 태그
  tervas_bridge.h     — 엔진 어댑터 인터페이스
  tervas_cli.h        — CLI 실행 인터페이스
  tervas_projection.h — tv_cell_visible, WH/BH 판별
  tervas_render.h     — 렌더러 인터페이스

src/tervas/
  tervas_core.c       — init/free (heap canvas)
  tervas_bridge.c     — snapshot (full/window/compact), inspect, region
  tervas_cli.c        — 전체 명령 파서 + 입력 검증
  tervas_projection.c — Projection 연산 (정수만)
  tervas_render_ascii.c — ASCII 렌더러 (검증용)
  tervas_main.c       — 독립 실행 바이너리

tests/
  test_tervas.c       — TV1~TV7 (7개 테스트)
```

---

## 8. 검증 기준 (Phase-7 완료 조건)

| 항목 | 결과 |
|------|------|
| `make tests/test_phase6` | PASS: 6 / FAIL: 0 ✅ |
| `make tests/test_tervas` | PASS: 7 / FAIL: 0 ✅ |
| `make test_all` | Phase-6 + Phase-7 전체 통과 ✅ |
| 엔진 canvas hash 불변 | TV7 검증 ✅ |
| OOB/bad input 오류 반환 | TV6 검증 ✅ |
| 결정론 (동일 snapshot → 동일 projection) | TV3 검증 ✅ |
| 윈도우 스냅샷 비용 감소 | TV2 검증 ✅ |
| AddressSanitizer (ASAN) | 누수 없음 ✅ |

---

## 9. 다음 Phase 권장 작업

1. **NCurses 렌더러 추가** — `tv_render_ncurses.c`, 마우스 클릭 → inspect 자동 연결
2. **diff-only snapshot** — 전체 복사 제거, Δ 누적 방식으로 전환
3. **tick 히스토리 탐색** — WH ring 전체를 Tervas에서 슬라이드
4. **region 동적 등록** — `region_register()` 연동

---

*CanvasOS Phase-7 Tervas Specification v1.0 — 이 문서는 코드와 함께 버전 관리되어야 한다.*
