# Phase-7 남은 리스크

## RISK-1: TvRenderCell[] 버퍼 비용 (MEDIUM)

| 모드 | 비용 | 대응 |
|------|------|------|
| TV_SNAP_FULL | 8MB memcpy/tick | TV_SNAP_WINDOW 전환 |
| TV_SNAP_WINDOW | O(viewport) | realloc 재사용 확인 필요 |
| TV_SNAP_COMPACT | O(active) | 좌표 정보 손실 주의 |

업그레이드: FULL → WINDOW → diff-only → mmap+CoW

## RISK-2: dispatch.h SSOT 강화 (LOW)

help 출력은 dispatch 기반 자동 생성 완료.
향후: 테스트 자동 생성, 문서 표 자동 export.

## RISK-3: Quick 명령 규약 (LOW)

Quick = 표준 명령의 별칭(alias). 매크로 아님, 조합 아님.
출력 포맷 = 표준 명령과 바이트 수준 동일.
Quick만 존재하고 표준 명령이 없는 것은 금지.
