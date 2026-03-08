#!/usr/bin/env python3
from __future__ import annotations
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
OUT = DIST / "meta" / "SHA256SUMS.txt"
OUT.parent.mkdir(parents=True, exist_ok=True)

def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()

targets = []
if DIST.exists():
    targets += [p for p in DIST.rglob("*") if p.is_file()]
for p in [ROOT/"VERSION", ROOT/"Makefile", ROOT/"README.md"]:
    if p.exists():
        targets.append(p)

targets = sorted(set(targets), key=lambda p: p.as_posix())
lines = []
for p in targets:
    rel = p.relative_to(ROOT)
    lines.append(f"{sha256_file(p)}  {rel.as_posix()}")

OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Wrote {OUT}")
