#!/usr/bin/env python3
"""Inline the WebAssembly engine and the instrument's factory presets into the page:
   python3 inline.py build/oneiroi.wasm page.html oneiroi-pocket.html   (pocket/build.sh runs it)"""
import base64, json, os, sys

wasm, page, out = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
presets = json.load(open(os.path.join(here, "..", "vst", "presets.json")))["presets"]
html = open(page, encoding="utf-8").read()
html = html.replace("%%WASM%%", base64.b64encode(open(wasm, "rb").read()).decode())
html = html.replace("%%PRESETS%%", json.dumps(presets, separators=(",", ":")))
open(out, "w", encoding="utf-8").write(html)
