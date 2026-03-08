#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate CanvasOS DevDictionary JSON from SSOT .def files.

Outputs:
  devdict_site/data/opcodes.json
  devdict_site/data/regions.json
  devdict_site/data/bindings.json
  devdict_site/data/index.json  (search index; behavior-first)
"""
import json, re, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
INC = ROOT / "include"
OUT = ROOT / "devdict_site" / "data"
OUT.mkdir(parents=True, exist_ok=True)

def parse_xmacros(path, kind):
  txt = path.read_text(encoding="utf-8")
  items = []
  if kind == "opcodes":
    rx = re.compile(r'^X\(([^,]+),\s*(0x[0-9A-Fa-f]+|\d+),\s*([^,]+),\s*\n\s*"([^"]*)",\s*\n\s*"([^"]*)",\s*\n\s*"([^"]*)"\s*\)\s*$', re.M)
    for m in rx.finditer(txt):
      name, code_s, cls, tags, kw, desc = m.groups()
      code = int(code_s, 0)
      items.append(dict(name=name.strip(), code=code, class_=cls.strip(), tags=tags, keywords=kw, desc=desc))
  elif kind == "regions":
    rx = re.compile(r'^R\(([^,]+),\s*([^,]+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*\n\s*"([^"]*)",\s*\n\s*"([^"]*)",\s*\n\s*"([^"]*)"\s*\)\s*$', re.M)
    for m in rx.finditer(txt):
      name, kind2, x0,y0,w,h,tags,kw,desc = m.groups()
      items.append(dict(name=name.strip(), kind=kind2.strip(), x0=int(x0), y0=int(y0), w=int(w), h=int(h), tags=tags, keywords=kw, desc=desc))
  elif kind == "bindings":
    rx = re.compile(r'^BIND\(([^,]+),\s*([^,]+),\s*([^,]+),\s*"([^"]*)"\)\s*$', re.M)
    for m in rx.finditer(txt):
      sym, tk, tn, note = m.groups()
      items.append(dict(symbol=sym.strip(), target_kind=tk.strip(), target=tn.strip(), note=note))
  return items

opcodes = parse_xmacros(INC/"canvasos_opcodes.def", "opcodes")
regions = parse_xmacros(INC/"canvasos_regions.def", "regions")
bindings = parse_xmacros(INC/"canvasos_bindings.def", "bindings")

(OUT/"opcodes.json").write_text(json.dumps(opcodes, ensure_ascii=False, indent=2), encoding="utf-8")
(OUT/"regions.json").write_text(json.dumps(regions, ensure_ascii=False, indent=2), encoding="utf-8")
(OUT/"bindings.json").write_text(json.dumps(bindings, ensure_ascii=False, indent=2), encoding="utf-8")

# behavior-first search index
index = []
for op in opcodes:
  index.append({
    "kind": "opcode",
    "key": op["name"],
    "code": op["code"],
    "class": op["class_"],
    "tags": op["tags"],
    "keywords": op["keywords"],
    "desc": op["desc"],
  })
for r in regions:
  index.append({
    "kind": "region",
    "key": r["name"],
    "tags": r["tags"],
    "keywords": r["keywords"],
    "desc": r["desc"],
    "pixel_box": {"x0": r["x0"], "y0": r["y0"], "w": r["w"], "h": r["h"]},
  })
(OUT/"index.json").write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding="utf-8")

print("OK: wrote devdict_site/data/*.json")
