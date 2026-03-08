#!/bin/bash
# CanvasOS Release Script
# Usage: ./scripts/release.sh [tag]
set -euo pipefail

TAG="${1:-$(cat VERSION 2>/dev/null || echo 'dev')}"
DIST_DIR="dist"
DIST_NAME="CanvasOS_${TAG}"

echo "=== CanvasOS Release: ${TAG} ==="

# 1. Clean
echo "[1/5] Clean..."
make clean 2>/dev/null || true
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

# 2. Build + Test
echo "[2/5] Build + Test..."
make test_all 2>&1 | grep -E "PASS:|FAIL:|==="
echo "  Standard build: OK"

# 3. Package
echo "[3/5] Package..."
make clean 2>/dev/null || true
ZIP="${DIST_DIR}/${DIST_NAME}.zip"
zip -r "${ZIP}" \
    src/ include/ tests/ examples/ docs/ scripts/ tools/ programs/ \
    devdict_site/ \
    Makefile VERSION README.md CHANGELOG.md \
    RELEASE_NOTES_*.md RELEASE_TAGGING.md PUSH_GUIDE.md \
    .gitlab-ci.yml \
    -x "tests/test_phase6/*" "tests/test_phase8/*" \
       "tests/test_phase9/*" "tests/test_phase10/*" \
       "tests/test_tervas/*" \
    2>/dev/null || true

# 4. Checksum
echo "[4/5] SHA256..."
sha256sum "${ZIP}" > "${ZIP}.sha256"
cat "${ZIP}.sha256"

# 5. Summary
echo "[5/5] Release Summary"
echo "  Tag:   ${TAG}"
echo "  Size:  $(du -h "${ZIP}" | cut -f1)"
echo "  Files: $(find src include -name '*.c' -o -name '*.h' | wc -l)"
echo "  Tests: $(grep -rc 'PASS()' tests/*.c 2>/dev/null | awk -F: '{s+=$2}END{print s}') cases"
echo "=== Done ==="
