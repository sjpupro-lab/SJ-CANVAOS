#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "[1/5] Environment check"
command -v clang >/dev/null 2>&1 || { echo "clang not found. Install: pkg install clang"; exit 1; }
command -v make  >/dev/null 2>&1 || { echo "make not found. Install: pkg install make"; exit 1; }

echo "[2/5] Clean"
if [ -f Makefile ] || [ -f makefile ]; then
  make clean || true
fi

echo "[3/5] Build (CC=clang)"
export CC=clang
export CXX=clang++
export CFLAGS="${CFLAGS:-} -O2"
export CXXFLAGS="${CXXFLAGS:-} -O2"

if make -n all >/dev/null 2>&1; then
  make all
else
  make
fi

echo "[4/5] Stage into dist/bin/termux"
mkdir -p dist/bin/termux

if [ -d build ]; then
  find build -maxdepth 2 -type f -perm -111 -exec cp -f {} dist/bin/termux/ \; || true
fi

find . -maxdepth 2 -type f -perm -111   ! -path "./.git/*" ! -path "./dist/*" ! -path "./scripts/*"   -exec cp -f {} dist/bin/termux/ \; || true

echo "[5/5] Write build metadata"
mkdir -p dist/meta
{
  echo "target=termux"
  echo "date=$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
  echo "cc=$CC"
  echo "cflags=$CFLAGS"
} > dist/meta/TERMUX_BUILDINFO.txt

echo "OK: dist/bin/termux populated."
