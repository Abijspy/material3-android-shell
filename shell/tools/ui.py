#!/usr/bin/env python3
"""Find UI elements on screen from `material3ui msg debug layout`.

  ui.py find TEXT [--surface NS] [--nth N] [--exact]
                                            print "X Y", the centre of the first
                                            node whose text contains (or is) TEXT
  ui.py at TYPE [--surface NS] [--nth N]     same, by node type (Island, Slider…)
  ui.py dump                                 the raw layout

Layer surfaces are placed from their anchors on a VPOINTER_EXTENT output
(default 1600x900), which is how the compositor positions them.
"""
import os, subprocess, sys

W, H = map(int, os.environ.get("VPOINTER_EXTENT", "1600x900").split("x"))
TOP, BOTTOM, LEFT, RIGHT = 1, 2, 4, 8


def layout():
    out = subprocess.run(["./build/material3ui", "msg", "debug"], capture_output=True, text=True).stdout
    surfaces = []
    # Full-area popups start under a top bar's exclusive zone (other zones,
    # a bottom bar or dock, only shorten them)
    top_zone = 0
    for l in out.splitlines():
        if l.startswith("surface material3ui-bar ") and "anchor=13" in l:
            top_zone = float(l.split()[2].split("x")[1])
    for line in out.splitlines():
        if line.startswith("surface "):
            parts = line.split()
            ns, (w, h) = parts[1], map(float, parts[2].split("x"))
            anchor, margin = 0, (0, 0, 0, 0)
            for p in parts[3:]:
                if p.startswith("anchor="):
                    anchor = int(p[7:])
                elif p.startswith("margin="):
                    margin = tuple(int(v) for v in p[7:].split(","))
            mt, mr, mb, ml = margin
            x = ml if anchor & LEFT else (W - w - mr if anchor & RIGHT else (W - w) / 2)
            y = mt if anchor & TOP else (H - h - mb if anchor & BOTTOM else (H - h) / 2)
            # Full-area popups sit below the bar's exclusive zone
            if anchor & TOP and anchor & BOTTOM:
                y = mt + top_zone
            surfaces.append({"ns": ns, "x": x, "y": y, "nodes": []})
        elif surfaces:
            f = line.split(" ", 6)
            if len(f) < 6:
                continue
            text = f[6] if len(f) > 6 else ""
            surfaces[-1]["nodes"].append({"type": f[1], "x": float(f[2]), "y": float(f[3]),
                                          "w": float(f[4]), "h": float(f[5]),
                                          "interactive": text.startswith("* "), "text": text[2:] if text.startswith("* ") else text})
    return surfaces


def main():
    args = sys.argv[1:]
    surface = args[args.index("--surface") + 1] if "--surface" in args else None
    nth = int(args[args.index("--nth") + 1]) if "--nth" in args else 0
    exact = "--exact" in args
    if args[0] == "dump":
        for s in layout():
            print(s["ns"], s["x"], s["y"])
            for n in s["nodes"]:
                print("   ", n)
        return
    matches = []
    for s in layout():
        if surface and s["ns"] != surface:
            continue
        for n in s["nodes"]:
            if args[0] == "find":
                hit = n["text"] == args[1] if exact else args[1] in n["text"]
            else:
                hit = n["type"] == args[1]
            if hit:
                matches.append((s["x"] + n["x"] + n["w"] / 2, s["y"] + n["y"] + n["h"] / 2))
    if len(matches) <= nth:
        sys.exit(f"ui.py: no match for {args[1]!r}")
    print(int(matches[nth][0]), int(matches[nth][1]))


main()
