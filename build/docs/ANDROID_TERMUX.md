# Android / Termux 빌드 가이드

이 프로젝트는 **Termux(Android)** 환경에서 `clang` 기반으로 로컬 빌드가 가능합니다.

## 1) Termux 패키지 설치
```bash
pkg update -y
pkg upgrade -y
pkg install -y git make clang python zip
```

## 2) 프로젝트 받기
```bash
git clone <REPO_URL>
cd <REPO_DIR>
```

## 3) 빌드 & dist 패키징
```bash
chmod +x scripts/build_android_termux.sh
./scripts/build_android_termux.sh
```

## 4) 결과물
- `dist/bin/termux/` 아래에 바이너리가 생성됩니다.
- `dist/` 전체를 zip로 묶어 배포할 수 있습니다.

## 트러블슈팅
- `permission denied`: `chmod +x scripts/*.sh` 재적용
- 빌드 실패 시: `make clean` 후 재시도
