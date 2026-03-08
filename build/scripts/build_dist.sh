#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(cat "$ROOT/VERSION" | tr -d '\r\n')"
DIST="$ROOT/dist"

echo "[build_dist] VERSION=$VERSION"
rm -rf "$DIST"
mkdir -p "$DIST/bin" "$DIST/docs" "$DIST/meta"

make -C "$ROOT" clean
make -C "$ROOT" -j all cli sjterm

for b in canvasos canvasos_cli sjterm; do
  if [ -f "$ROOT/$b" ]; then
    cp -v "$ROOT/$b" "$DIST/bin/"
  fi
done

# docs
if [ -d "$ROOT/docs" ]; then
  cp -rv "$ROOT/docs" "$DIST/"
fi
if [ -d "$ROOT/devdict_site" ]; then
  cp -rv "$ROOT/devdict_site" "$DIST/"
fi

# include minimal headers/sources/tests (optional but useful)
cp -rv "$ROOT/include" "$DIST/"
cp -rv "$ROOT/src" "$DIST/"
cp -rv "$ROOT/tests" "$DIST/"

# build info
{
  date -u +"UTC build time: %Y-%m-%dT%H:%M:%SZ"
  echo "Host: $(uname -a)"
  echo "Compiler: $(gcc --version 2>/dev/null | head -n 1 || true)"
  echo "Make: $(make --version 2>/dev/null | head -n 1 || true)"
  echo "Version: $VERSION"
} > "$DIST/meta/BUILDINFO.txt"

echo "[build_dist] OK -> $DIST"
