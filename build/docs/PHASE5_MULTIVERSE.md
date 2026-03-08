# Phase 5: Multiverse Engine Specification

## 0. 결정론 커널 규약 (Determinism Kernel Contract) — Phase 5 필수

Phase 5는 Lane/Branch/WH·BH/Δ-Commit/Merge로 "멀티버스"를 만들기 때문에,
성능 최적화보다 먼저 **결정론 생존 규약**을 문서+코드에 고정해야 한다.

| 규약 | 내용 | 코드 강제 |
|---|---|---|
| [DK-1] Tick Boundary | Δ/Merge/BH는 WH 프레임 경계에서만 | TickBoundaryGuard + ASSERT_TICK_BOUNDARY |
| [DK-2] Integer Only | float/double 금지, GPU도 rgba8ui | DK_INT(_Generic) |
| [DK-3] Fixed Order | cell_index 오름차순만 허용 | dk_cell_index(), qsort |
| [DK-4] Normalize | G채널 결과 clamp 필수 | DK_CLAMP_U8/U16/U32 |
| [DK-5] Noise Floor | ±1 차이는 결정론 평균 | DK_ABSORB_NOISE |

---

## 1. 개요

Phase 5는 단일 Canvas(1024×1024) 위에서 **여러 실행 흐름(Lane)** 을
동시에 구동하고, **채널 깊이(plane_mask)** 로 독립 우주(Universe)를
나누는 Multiverse Engine을 구현한다.

## 2. 핵심 3축

### 2.1 Lane 축 (A 채널 상위 8비트)
- LaneID = Cell.A[31:24]
- 최대 256 Lane / Canvas
- Lane별 독립 WH/BH Zone 배치 가능
- Gate는 Lane별 gate_start..gate_start+gate_count로 격리
- **구현됨**: lane_tick() A 채널 필터링 (canvas_lane.c)

### 2.2 Universe 축 (plane_mask)
- PageSelector.plane_mask = {A,B,G,R} 비트 조합
- 2^4 = 16 Universe 정의 가능
- 동일 Canvas 메모리를 다른 채널 렌즈로 해석
- 데이터 복사 없음 — plane_mask 전환만

### 2.3 Y축 (시간)
- Y 좌표 = tick 구간 (y_tick_stride로 스케일 조정)
- WH record y = tick % WH_H → 이미 Y가 시간
- Branch의 y_min..y_max = 시간 구간 선택

## 3. BH 시간 의미 압축

BH는 "삭제"가 아니라 **기술(description)로 치환**이다.

```
BH_RULE_IDLE  : 무변화 구간 → "N틱 IDLE" 1레코드
BH_RULE_LOOP  : K회 반복   → "패턴P, K회, stride=S" 1레코드  
BH_RULE_BURST : 폭주 이벤트 → "T_start, count=M" 1레코드
```

치환 결과는 WH에 WH_OP_BH_SUMMARY(0x40)로 기록 (감사/재현 가능).
원본 구간 해시 보존 (완전 삭제 금지).

## 4. Merge 정책 확정

```
MERGE_LAST_WINS   — 마지막 Δ 덮어쓰기 (기본)
MERGE_FIRST_WINS  — 최초 Δ 유지
MERGE_ADDITIVE_G  — G 누적 + DK_CLAMP_U8
MERGE_MAX_G       — G 최대값
MERGE_LOCK_WINS   — gate CLOSE 우선 (보안)
MERGE_CUSTOM      — RuleTable 기반
```

충돌 순위: gate CLOSE → plane_mask 낮은 bit → LAST_WINS
충돌 처리: CONFLICT_RECORD(기본, 감사) / GATE_CLOSE / IGNORE

## 5. 용량 이론

```
단일 Canvas 기준:
  WH 레코드: 256 Lane × 32,768 = 8,388,608
  Universe: 16 × 8M = 134,217,728 이벤트
  BH 압축 후 실효 용량: 이론상 10~100x 확장
  CanvasFS slot: ~499,200 → 외부 BlobStore 참조
```

## 6. 외부 파일 체인(BlobStore) 경계

- 외부 체인: `BlobStore/Mount Adapter` 로만 (결정론 경계 밖)
- CanvasFS: 내부 표준/진실의 원천 (결정론 경계 안)
- 외부→내부 커밋 시 캔버스 규격으로 정규화 + lock/CRC 검증

## 7. Phase 5 구현 현황

| 항목 | 상태 | 파일 |
|---|---|---|
| canvas_determinism.h | ✅ 완성 | DK-1..5 모든 규약 |
| canvas_bh_compress.h/c | ✅헤더+IDLE구현 / ✗LOOP/BURST | canvas_bh_compress.* |
| canvas_merge.h/c | ✅ 완성 (6개 정책) | canvas_merge.* |
| canvas_lane.c lane_tick() | ✅ A 채널 필터링 구현 | canvas_lane.c |
| canvas_branch.c | 🔄 스켈레톤 | canvas_branch.c |
| canvas_multiverse.c | 🔄 스켈레톤 | canvas_multiverse.c |
| test_phase5.c | ✅ 8개 테스트 | tests/test_phase5.c |
| mve_save_meta() CVP SEC=5 | ❌ TODO | canvas_multiverse.c |

## 8. GPU 연동 (Phase 6 예고)

Canvas = 8MB = GPU L2 캐시 크기.
Cell = rgba8ui texel (DK-2 정수 보장).

```glsl
layout(rgba8ui) uniform uimage2D u_canvas;
uniform uint u_lane_id;   // Lane 필터
uniform uint u_plane_mask; // Universe 필터
```

lane branch = uniform branch → warp divergence 없음.
