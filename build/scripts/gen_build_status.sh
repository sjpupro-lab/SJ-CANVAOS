#!/bin/bash
# gen_build_status.sh — Generate build status JSON for devdict_site dashboard
# Usage: bash scripts/gen_build_status.sh [output_path]
#
# Runs all test suites and captures results, then writes build_status.json

set -e
cd "$(dirname "$0")/.."

OUT="${1:-devdict_site/data/build_status.json}"
VERSION=$(cat VERSION 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date +"%Y-%m-%d")
CC_VER=$(${CC:-clang} --version 2>/dev/null | head -1 || echo "unknown")
ARCH=$(uname -m 2>/dev/null || echo "unknown")
OS_INFO=$(uname -s 2>/dev/null || echo "unknown")

# Test suite definitions: name, binary, expected_pass_count
SUITES=(
  "Phase-6:tests/test_phase6:6"
  "Phase-7 Tervas:tests/test_tervas:10"
  "Phase-8:tests/test_phase8:18"
  "Phase-9:tests/test_phase9:20"
  "Phase-10:tests/test_phase10:20"
  "Bridge:tests/test_bridge:16"
  "Patch-B:tests/test_patchB:10"
  "Patch-C:tests/test_patchC:10"
  "Patch-D:tests/test_patchD:10"
  "Patch-E:tests/test_patchE:10"
  "Patch-F:tests/test_patchF:10"
  "Patch-G:tests/test_patchG:10"
  "Patch-H:tests/test_patchH:10"
)

TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0
RESULTS="["

first=true
for suite in "${SUITES[@]}"; do
  IFS=':' read -r name bin expected <<< "$suite"

  if [ ! -x "$bin" ]; then
    # Try to build
    make "$bin" CC="${CC:-clang}" 2>/dev/null || true
  fi

  if [ -x "$bin" ]; then
    output=$(./"$bin" 2>&1) || true
    pass=$(echo "$output" | grep -oP 'PASS:\s*\K[0-9]+' | tail -1)
    fail=$(echo "$output" | grep -oP 'FAIL:\s*\K[0-9]+' | tail -1)
    pass=${pass:-0}
    fail=${fail:-0}
    status="pass"
    [ "$fail" -gt 0 ] && status="fail"
    TOTAL_PASS=$((TOTAL_PASS + pass))
    TOTAL_FAIL=$((TOTAL_FAIL + fail))
  else
    pass=0
    fail=0
    status="skip"
    TOTAL_SKIP=$((TOTAL_SKIP + expected))
  fi

  $first || RESULTS+=","
  first=false
  RESULTS+=$(cat <<ENTRY
  {
    "name": "$name",
    "binary": "$bin",
    "expected": $expected,
    "pass": $pass,
    "fail": $fail,
    "status": "$status"
  }
ENTRY
)
done

RESULTS+="]"

# Write JSON
cat > "$OUT" <<EOF
{
  "version": "$VERSION",
  "build_date": "$BUILD_DATE",
  "compiler": "$CC_VER",
  "arch": "$ARCH",
  "os": "$OS_INFO",
  "total_pass": $TOTAL_PASS,
  "total_fail": $TOTAL_FAIL,
  "total_skip": $TOTAL_SKIP,
  "suites": $RESULTS
}
EOF

echo "[gen_build_status] wrote $OUT  (pass=$TOTAL_PASS fail=$TOTAL_FAIL skip=$TOTAL_SKIP)"
