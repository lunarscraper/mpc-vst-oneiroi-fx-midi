#!/usr/bin/env python3
"""presets.json from presets.src.json: MPC's PRESET menu sets only the parameters a preset lists, so every preset
gets every parameter (its own values over the defaults in params.json).   python3 vst/make_presets.py"""
import json, os
d = os.path.dirname(os.path.abspath(__file__))
params = json.load(open(os.path.join(d, "params.json")))["params"]
src = json.load(open(os.path.join(d, "presets.src.json")))["presets"]
out = []
for p in src:
    unknown = set(p["values"]) - {q["key"] for q in params}
    assert not unknown, (p["name"], unknown)
    vals = {}
    for q in params:
        if q.get("momentary") or q.get("step_of"):
            continue
        v = p["values"].get(q["key"], q.get("default", 0))
        if q.get("options") and not isinstance(v, str):
            v = q["options"][int(v)]
        vals[q["key"]] = v
    out.append({"name": p["name"], "values": vals})
json.dump({"presets": out}, open(os.path.join(d, "presets.json"), "w"), indent=1)
print("presets.json:", len(out), "presets")
