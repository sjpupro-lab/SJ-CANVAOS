# SPEC: CanvasOS QuadBoot v1

## 1. 개요

CanvasOS는 1024x1024 Cell 격자 위에서 동작하는 결정론적 타일 실행 시스템이다.
중심(512,512)에서 시작하는 Ring(MH) 스캔을 통해 모든 Cell을 순회하며,
각 Cell의 A_word에 따라 게이트 개폐·분기·I/O를 수행한다.

## 2. 기본 용어

| 용어 | 정의 |
|------|------|
| Cell | 1024x1024 격자의 단일 타일. 좌표 (x,y), 0<=x,y<1024 |
| A_word | 32비트: [31:24]=Gate, [23:16]=Opcode, [15:0]=Arg |
| TileGate | 4096개 게이트 슬롯. 기본 CLOSE, 명시적 OPEN만 효과 |
| B-page | 256행 ROM. 각 행 = 규칙(Rule) 1개 |
| CVP | CanVas Page. ABGR 순서 바이너리 포맷 |

## 3. A_word 비트 구조

    [31:24]  GateID   (8비트, 0x00~0xFF)
    [23:16]  Opcode   (8비트)
    [15: 0]  Arg      (16비트)

    Opcode 테이블:
    0x00  NOP      아무것도 하지 않음
    0x01  OPEN     게이트 GateID를 OPEN 상태로 설정
    0x02  CLOSE    게이트 GateID를 CLOSE 상태로 설정
    0x03  BRANCH   Arg 좌표로 스캔 포인터 점프
    0x04  LOAD     CVP에서 Cell 로드
    0x05  SAVE     CVP로 Cell 저장
    0xFF  HALT     실행 중지

## 4. QuadBoot 시퀀스

    1. 파워온 -> Cell(512,512) 로드
    2. Ring(MH) 스캔 초기화: radius=0, dir=EAST
    3. 각 Cell: A_word 디코드 -> Gate 확인 -> OPEN이면 실행
    4. 스캔 완료 후 -> B-page 룰 적용
    5. CVP 세이브 (ABGR 순)
    6. 다음 프레임 반복

## 5. TileGate 규칙

- 총 4096개 슬롯 (GateID 0x000 ~ 0xFFF)
- 초기 상태: 전부 CLOSE
- OPEN 명령 수신 시에만 해당 슬롯 OPEN
- CLOSE는 명시적 CLOSE 명령 또는 리셋 시
- Gate CLOSE 상태의 Cell은 Opcode 무시

## 6. CVP I/O 포맷

    파일 헤더 (8 bytes):
      [0..1]  매직 "CV"
      [2..3]  버전 0x0001
      [4..5]  너비 (LE16)
      [6..7]  높이 (LE16)

    픽셀 데이터 (너비 x 높이 x 4 bytes):
      각 픽셀: A_word (Alpha=Gate) -> B -> G -> R
      저장 순서: 행 우선(row-major), 좌상단 기준

## 7. B-page ROM 구조

    총 256행. 각 행 = 16 bytes:
      [0..3]   조건 마스크 (A_word AND mask)
      [4..7]   조건 값    (masked == value 이면 실행)
      [8..11]  실행 A_word
      [12..15] 예약

