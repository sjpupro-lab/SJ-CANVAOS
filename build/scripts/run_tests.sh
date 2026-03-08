#!/usr/bin/env bash
set -euo pipefail
echo "[run_tests] build + run unit tests"
make clean
make -j all cli sjterm
make -j test

# ensure exec bits (some environments mount as noexec/strip bits)
chmod +x canvasos canvasos_cli sjterm 2>/dev/null || true
chmod +x tests/* 2>/dev/null || true

if [ -f tests/test_phase6.c ]; then
  make -j tests/test_phase6
  chmod +x ./tests/test_phase6 2>/dev/null || true
  ./tests/test_phase6
fi
if [ -f tests/test_delta_merge.c ]; then
  make -j tests/test_delta_merge
  chmod +x ./tests/test_delta_merge 2>/dev/null || true
  ./tests/test_delta_merge
fi

echo "[run_tests] OK"
