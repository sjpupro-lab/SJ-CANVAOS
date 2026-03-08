# SJ CANVAS OS — Spec-Based TODO Checklist (Phase 5 정합판)

**고정 규칙**
- 이 문서의 항목은 **완수(✅) 체크 전 삭제 금지**
- 체크는 `- [ ]` → `- [x]` 로만 수행
- 항목 변경이 필요하면 삭제하지 말고 **Progress Log에 수정 기록** 추가

---

## 0) Phase 5 스켈레톤 존재/부팅 확인 (현재 스켈레톤 기준)

- [x] `include/` Phase 5 헤더 3종 존재 (`canvas_lane.h`, `canvas_branch.h`, `canvas_multiverse.h`)
- [x] `src/` Phase 5 구현 스켈레톤 3종 존재 (`canvas_lane.c`, `canvas_branch.c`, `canvas_multiverse.c`)
- [x] `lane_table_init()` 즉시 호출 가능
- [x] `mve_init()` 즉시 호출 가능
- [x] `mve_print_capacity()` 즉시 호출 가능
- [x] `docs/PHASE5_MULTIVERSE.md` 존재

---

## 1) 결정론 규약(Determinism Kernel Contract) — Phase 5에 **반드시** 추가

> Phase 5는 GPU/Δ-Commit/Merge/WH·BH 때문에 “성능”과 동시에 “결정론 붕괴” 위험이 커짐.
> 아래 규약이 문서+코드에 고정되기 전에는 병렬/압축/머지 최적화를 진행하지 않는다.

- [ ] **RULE/Δ-Commit/Merge/BH는 틱(WH 프레임 경계)에서만 실행** (중간 구간 실행 금지)
- [ ] **정수 연산만 허용**(float/부동소수점 금지) — 최소한 “결정론 경계 내부”에서는 강제
- [ ] **리덕션/합산/병합 순서 고정**(예: cell_index 오름차순) — GPU/병렬 대비
- [ ] **결과 정규화(Normalize)** 필수: clamp/round 규칙을 명세로 고정
- [ ] **오염(Noise) 허용 범위**를 명세로 고정 (예: ±1 흡수 규칙 등)

---

## 2) BH(BlackHole) 의미 압축 명세 — “삭제”가 아니라 “치환”

현재 Phase 5 문서/헤더에는 WH/BH Zone 배치가 있으나, **BH의 의미(시간 압축 규칙)가 명세로 고정되어 있지 않음.**
아래를 먼저 고정해야 WH 포화 문제를 안정적으로 해결할 수 있음.

- [ ] BH는 “원본 이벤트 삭제”가 아니라 **기술(description)로 치환**임을 명세로 고정
- [ ] BH 수행은 **틱 경계에서만** 실행하고, BH 결과 자체를 WH에 기록(감사/재현)
- [ ] 최소 3종 BH 규칙 명세
  - [ ] `BH_SUMMARIZE_IDLE` (무변화 구간 요약)
  - [ ] `BH_SUMMARIZE_LOOP` (반복 패턴 루프 요약)
  - [ ] `BH_SUMMARIZE_BURST` (폭주 이벤트 요약)
- [ ] 요약 후에도 **참조 링크(인덱스/해시)** 보존 규칙 고정 (완전 삭제 금지)

---

## 3) Branch(Δ-Commit / MergePolicy) — 정합성 강화 TODO

- [x] `DeltaCommit` 구조체에 before/after/tick 포함
- [x] MergePolicy enum 존재

남은 정합 TODO:
- [ ] **Merge는 틱 경계에서만** 수행 (코드에서 강제)
- [ ] 충돌 정책 고정(“더 제한적 정책 승리” 등) + 충돌 시 처리(WH 기록/게이트 닫기/무시 중 1택)
- [ ] Δ-Commit 기록 형식의 **정규화**(동일 셀 여러번 변화 시 축약 규칙) 명세
- [ ] MergePolicy 최소 세트 확정 + 결정론 테스트 추가

---

## 4) Lane(Execution Lane) — 격리/스케줄링/우선도

- [ ] Lane별 gate 범위 격리 규칙을 문서에 고정 (gate_start..gate_start+gate_count)
- [ ] Lane tick 실행 시 **캔버스 필터링 규칙**(A[31:24]=LaneID)이 코드/테스트로 고정
- [ ] “pressure/priority” 필드(또는 동등) 추가: Lane 간 실행 순서 결정(단일 스레드에서도 대규모 태스크처럼 동작)

---

## 5) 외부 파일 체인 참조(CanvasFS slot → 외부 blob) — 경계 명세

Phase 5 헤더 주석에 “Lane이 CanvasFS slot을 통해 외부 데이터 참조” 개념이 등장함.
이 방향은 가능하지만, CanvasOS의 본질(단일 표현 공간/결정론)을 유지하려면 경계가 필요함.

- [ ] 외부 체인은 **백킹 스토어(BlobStore/Mount Adapter)** 로만 정의한다
- [ ] CanvasFS slot은 “진실의 원천”으로 유지: 커밋 시 내부 규격(캔버스)로 정규화
- [ ] 외부 의존으로 결정론이 깨지지 않도록 lock/CRC/해시 규칙 명세

---

## 6) DevDict 포털(풀텍스트) — Phase 5 통합 체크

- [ ] `Phase 5` 탭 추가 (Lane/Branch/Multiverse 심볼/문서 노출)
- [ ] docs 본문 풀텍스트 인덱싱(섹션 단위) + 검색 결과에 하이라이트
- [ ] 아키텍처 맵(공간 지도)에서 클릭 → 심볼/문서 검색으로 점프
- [ ] 용량 바(Lane/Universe/WH_CAP/슬롯 수) 표시

---

## Progress Log (삭제 금지)
- 2026-03-02: Phase 5 스켈레톤(zip) 기반으로 정합 TODO 재구성. 스켈레톤 존재/기본 함수는 체크 완료.
