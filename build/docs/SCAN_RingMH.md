# SCAN: Ring(MH) 수학 정의

## 1. 개요

Ring(MH)는 Manhattan Distance 기반 나선형 스캔이다.
중심 (cx, cy)에서 시작하여 반경 r=0,1,2,... 순서로
각 링(Chebyshev shell)의 셀을 결정론적으로 열거한다.

## 2. 수학 정의

  Center: (cx, cy) = (512, 512)

  반경 r의 Ring = { (x,y) | max(|x-cx|, |y-cy|) == r }

  열거 순서 (시계방향):
  1. 상단 변: y=cy-r, x: cx-r -> cx+r
  2. 우측 변: x=cx+r, y: cy-r+1 -> cy+r
  3. 하단 변: y=cy+r, x: cx+r-1 -> cx-r
  4. 좌측 변: x=cx-r, y: cy+r-1 -> cy-r+1

## 3. 결정론 보장 조건

- 동일한 (cx,cy)와 격자 크기에서 항상 동일한 순서
- r=0: 단일 셀 (cx,cy) 하나만 방문
- 경계 클리핑: 격자 범위 [0,W) x [0,H) 벗어나면 건너뜀
- 방문 순서 절대 변경 불가

## 4. 전체 셀 수

  1024 x 1024 격자 기준:
  r=0: 1개, r=k: 8k개 (경계 미달 제외), 최대 r=511
  총합: 1,048,576 = 1024^2

## 5. 의사코드

  function ring_mh_scan(grid, cx, cy, callback):
    callback(cx, cy)
    for r = 1 to MAX_RADIUS:
      y = cy - r
      for x = cx-r to cx+r:
        if in_bounds(x,y): callback(x,y)
      x = cx + r
      for y = cy-r+1 to cy+r:
        if in_bounds(x,y): callback(x,y)
      y = cy + r
      for x = cx+r-1 downto cx-r:
        if in_bounds(x,y): callback(x,y)
      x = cx - r
      for y = cy+r-1 downto cy-r+1:
        if in_bounds(x,y): callback(x,y)

---

## 6. Phase 0 알고리즘 주의사항 (known issue)

현재 spec 4.2 구현의 MH Ring에서 **x축 포인트(dy=0, dx!=0)가 생성되지 않는다.**

원인: 각 쿼드런트는 t=0..d-1을 순회하는데,
dx=t, dy=d-t 에서 dy=0이 되려면 t=d 가 필요하지만 t < d 조건으로 제한됨.

영향:
  - (513,512), (514,512), ... (x축 방향 셀)이 스캔에서 누락
  - y=512 행에 HELLO를 심어도 (512,512) 첫 글자만 실행됨
  - test_scan은 통과: 링마다 2개 중복 + 2개 누락이 상쇄되어 총합=1,048,576

Phase 1 수정 방향:
  - 각 쿼드런트 t 범위를 0..d 로 확장하고 경계 중복 제거, 또는
  - 쿼드런트 분할 방식을 4변 열거로 교체 (Chebyshev shell처럼)
