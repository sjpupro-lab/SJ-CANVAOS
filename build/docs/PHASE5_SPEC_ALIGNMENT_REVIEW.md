# Phase 5 스켈레톤 — 명세 정합 리뷰 (요약 + 수정 설계안)

작성 기준: `SJCANVOS_phase5_skeleton.zip` (Phase 5 헤더/스켈레톤 포함)

---

## 1) 결론

Phase 5 스켈레톤의 방향(Lane/Branch/Multiverse + WH/BH Zone 배치)은 **명세 방향과 대체로 정합**함.
다만 아래 3개는 “성능 기능”이 아니라 “결정론 생존 장치”이므로 **Phase 5 최상단 규약으로 먼저 고정**해야 함.

1) RULE/Δ-Commit/Merge/BH는 **틱(WH 프레임 경계)에서만** 실행  
2) 병렬/GPU는 **정수 연산 + 순서 고정 + 결과 정규화** 없이 도입 금지  
3) BH는 삭제가 아니라 **시간 의미 압축(치환) + 참조 보존**

---

## 2) 스켈레톤에서 이미 정합한 부분(체크)

- LaneID = A[31:24] (A=주소/메타 강화)  
- Branch = “복사 없는 우주” (plane_mask/PageSelector 전환)  
- Δ-Commit 구조체에 before/after/tick 포함  
- WH/BH Zone 구조체 및 Lane×Universe 배치 아이디어 존재  
- 즉시 동작 함수: `lane_table_init`, `mve_init`, `mve_print_capacity`

---

## 3) 정합 위험 포인트(수정 설계 제안)

### 3.1 GPU dispatch 힌트(phase6 예고) — 결정론 규약 필요
`canvas_lane.h` 주석에서 Cell을 “GPU float4 texel”로 비유하고 있으나,
결정론 경계 내부에서 float/비결정 스케줄은 결과 불일치 위험이 큼.

**수정 설계(권장):**
- 결정론 커널 내부는 **u32/u16 정수 only**
- GPU는 `rgba8ui/rgba16ui` 등 정수 텍셀 기반으로만 사용
- 병합/리덕션은 **순서 고정**(tile_id → cell_index 오름차순)으로만 수행
- 결과는 normalize(clamp/round) 규칙을 통과

### 3.2 MergePolicy/Δ-Commit — 틱 경계 고정 + 축약 규칙 필요
Δ 레코드를 WH에 쌓는 구조는 좋지만, 아래가 명세로 고정되지 않으면 “민감도 폭발” 위험이 있음.

**수정 설계(권장):**
- Merge는 **틱 경계에서만**(mid-tick merge 금지)
- 동일 tick 내 동일 셀에 여러 Δ가 있을 경우:
  - (정책) 마지막 Δ만 유지 / 또는 before는 최초 1회만 유지
- 충돌 정책 고정(예: MERGE_OVERWRITE라도 “LOCK 계열 룰이 우선”)

### 3.3 BH 의미(압축) 명세 부재
현재 Phase 5 문서/헤더는 WH/BH Zone “배치”는 있으나,
BH가 무엇을 어떻게 압축하는지(치환/요약/링크 보존)가 명세로 드러나지 않음.

**수정 설계(권장):**
- BH는 “삭제”가 아니라 “요약 이벤트로 치환”
- BH 수행 자체를 WH에 기록(감사/재현)
- 최소 규칙 3종: IDLE/LOOP/BURST
- 요약 후에도 ref(인덱스/해시) 보존

---

## 4) CanvasFS slot → 외부 파일 체인 참조 방향성

질문: “CanvasFS slot이 외부 파일을 체인으로 참조하는 방향이 맞나?”

**정답(권장 구조):**
- 외부 파일 체인은 `BlobStore/Mount Adapter` 로만 둔다(결정론 경계 밖)
- CanvasFS slot은 **내부 표준(진실의 원천)** 유지(결정론 경계 안)
- 외부에서 읽어온 데이터는 “커밋 시” 캔버스 규격으로 정규화하여 저장

즉, 외부 체인 참조는 “OS 위에 올리는 앱 기능”이라기보다
**백킹 스토리지(저장 매체) 확장**으로 보는 것이 Phase 5 방향성과 잘 맞음.

---

## 5) 다음 액션(추천 구현 순서)

1) `docs/`에 **Determinism Kernel Contract** 섹션 추가(틀 고정)  
2) `BH` 의미 압축 규칙 3종 명세 + WH 기록 규약 추가  
3) MergePolicy “틱 경계 실행/축약 규칙/충돌 정책” 고정  
4) 그 다음에야 GPU/병렬/대용량 최적화

---

## 6) 관련 TODO
- `docs/TODO_SPEC_PHASE5_FIXED.md` 참고 (삭제 금지 체크리스트)
