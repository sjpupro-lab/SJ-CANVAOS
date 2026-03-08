# Phase 6 Double-Check Integration Report

작성일: 2026-03-03

대상:
- Base: `SJCANVOS_phase5_v3.zip`
- New files: `SJCANVOS_p6_new_files.zip`

결과:
- ✅ 신규 파일 8개 병합 완료
- ✅ 빌드 통합(캔버스 본체 + sjterm) 성공 (경고 일부)
- ✅ 테스트 타겟 `tests/test_phase6` 빌드 성공
- ⚠️ `tests/test_phase6` 실행은 로직에 따라 장시간/무한루프 가능성이 있어 자동 실행은 중단(타임아웃)

---

## 1) 병합된 신규 파일 (8개)
- `include/canvasos_workers.h`
- `src/workers.c`
- `include/canvas_gpu.h`
- `src/canvas_gpu_stub.c`
- `include/sjptl.h`
- `src/sjptl_parser.c`
- `src/sjterm.c`
- `tests/test_phase6.c`

---

## 2) 통합 과정에서 발견된 “컴파일/링크 필수 수정” (즉시 패치 적용)

### 2.1 `canvas_lane.h`의 WhRecord 타입 불일치 (치명)
문제:
- `engine_time.h`에서 `typedef struct {...} WhRecord;`로 정의돼 있는데,
- `canvas_lane.h`에서 `struct WhRecord*`를 사용하며 필드 접근을 시도 → 컴파일 에러

조치(적용됨):
- `include/canvas_lane.h`에 `#include "engine_time.h"` 추가
- `struct WhRecord` → `WhRecord`로 타입 명을 정정

### 2.2 `canvas_branch.c`에서 `LANE_F_ACTIVE` 미정의 (치명)
문제:
- `canvas_branch.c`에서 `LANE_F_ACTIVE` 사용
- `canvas_branch.h`가 `canvas_lane.h`를 include 하지 않아 macro/enum이 누락

조치(적용됨):
- `include/canvas_branch.h`에 `#include "canvas_lane.h"` 추가

### 2.3 `pthread_barrier_t` 미지원/미노출 환경 대응 (치명)
문제:
- 일부 환경에서 `pthread_barrier_t`가 컴파일 시 노출되지 않음(unknown type)

조치(적용됨):
- `include/canvasos_workers.h`에 **포터블 배리어**(`SJBarrier`) 정의 추가
- `src/workers.c`에 `sjbarrier_init/destroy/wait` 구현 추가
- workers.c 내부의 `pthread_barrier_*` 호출을 `sjbarrier_*`로 치환

### 2.4 테스트 링킹 시 `main` 중복 (치명)
문제:
- `src/engine.c` 안에 데모 `main()`이 존재하여
- `tests/test_phase6.c`의 `main()`과 충돌

조치(적용됨):
- `src/engine.c`의 `main()`을 `#ifdef ENGINE_DEMO_MAIN`으로 감싸 기본 빌드에서는 제외

---

## 3) 빌드 시스템 반영 (Makefile 패치 적용)
적용 내용:
- ENGINE_SRC에 Phase 6 관련 소스 포함:
  - `src/workers.c`
  - `src/canvas_gpu_stub.c`
  - `src/sjptl_parser.c`
  - (lane/merge/multiverse/branch/bh 소스도 링크 누락 방지 차원에서 포함)
- `LDFLAGS = -lpthread` 추가
- `sjterm` 바이너리 타겟 추가
- `tests/test_phase6` 타겟 추가

---

## 4) 남은 “사용자 구현 체크포인트”(의도된 TODO)
- `sjterm.c`의 `!` 커밋이 **WH_EDIT_COMMIT**에 어떤 포맷으로 기록되는지:
  - 현재 레포의 `wh_write_record()`/`WhOpcode` 확장 필요 가능
- 멀티스레드 결정론:
  - lane 범위 분할 정책을 최종 고정(워커별 lane_range)
  - merge 순서 고정(오름차순) 강제
- GPU 경로:
  - Phase 6은 stub이므로 실제 GPU backend 연결은 이후 추가 구현

---

## 5) 주의(현재 경고)
- `canvas_determinism.h`에서 unused param warning 존재(동작에는 영향 없음)

---

## 6) 산출물
- 병합 + 빌드 패치가 적용된 레포 ZIP: `SJCANVOS_phase6_bootstrap_merged.zip`
- 본 보고서: `docs/PHASE6_DOUBLECHECK_REPORT.md`
