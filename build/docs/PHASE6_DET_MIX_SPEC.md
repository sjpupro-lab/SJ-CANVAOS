# Phase 6+ Deterministic Mixing Spec (Lane/Page/Bpage + Host I/O)

핵심:
- 결정론 코어(DC)와 비결정론 어댑터(NA)를 분리한다.
- NA는 외부 이벤트를 **WH_OP_IO_EVENT**로 캡처한다.
- DC는 tick boundary에서만 이를 수집/정렬/주입한다.
- 멀티스레드(Lane) 실행은 “쓰기 소유권 + Δ-Commit + 고정 Merge 순서”로 결정론 유지.

## WH_OP_IO_EVENT 인코딩(WhRecord 2-cell)
- C0.A: apply_tick
- C0.B: WH_OP_IO_EVENT
- C0.G: lane_id low8 (Phase 6 최소)
- C0.R: dev(WhDevice)
- C1.A: ref(슬롯/블롭 id)
- C1.G: op(WhDeviceOp)
- C1.R: len low8

> page_id, len high bits 등 확장은 Phase 6 이후에 flags/target_kind/param packing으로 확장.

## 정렬 규칙
(tick, lane_id, page_id, dev, op, ref) 오름차순 고정.

## API
- `wh_push_io_event()` : NA -> WH 캡처
- `inject_collect/sort/apply()` : DC tick boundary 주입
- `BpageTable` : B 해석 테이블(규칙/옵/IO 매핑)
- `lane_exec_tick/merge_tick` : 워커 실행 + 경계 merge 스켈레톤
