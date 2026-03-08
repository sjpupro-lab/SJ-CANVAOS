# CanvasOS DevDictionary (Static)

Generate JSON:
- `python3 tools/gen_devdict.py`

Data outputs:
- `devdict_site/data/index.json` (behavior-first search index)
- `devdict_site/data/opcodes.json`
- `devdict_site/data/regions.json`
- `devdict_site/data/bindings.json`

Next:
- Add a small static UI (Docusaurus/MkDocs/Vite) that loads these JSON files.
