#!/usr/bin/env python3
"""Make shell/data/maps/world.svg, the time zone map, from Natural Earth's
1:50m land, lakes and country borders (public domain): three layers by id
(#land, #lakes, #borders) in plate carree (x = longitude, y = -latitude),
cropped to the inhabited latitudes and simplified.

    for f in ne_50m_land ne_50m_lakes ne_50m_admin_0_boundary_lines_land; do
        curl -LO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/$f.geojson
    done
    generate_world_map.py . shell/data/maps/world.svg

The map's code expects these latitudes (kNorth and kSouth in
shell/src/shell/TimeZoneMap.cpp) and draws each layer in its own colour."""
import json
import os
import sys

NORTH, SOUTH = 84.0, -58.0
TOLERANCE = 0.05     # degrees: about a pixel at twice the popup's size
MIN_ISLAND = 0.01    # square degrees
MIN_LAKE = 0.25      # square degrees: the big lakes (Great Lakes, Victoria, Baikal…)


def simplify(points, tol):
    """Douglas-Peucker, iterative"""
    if len(points) < 3:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = points[a]
        bx, by = points[b]
        dx, dy = bx - ax, by - ay
        norm = (dx * dx + dy * dy) ** 0.5
        best, at = -1.0, -1
        for i in range(a + 1, b):
            px, py = points[i]
            if norm == 0:
                d = ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
            else:
                d = abs(dy * px - dx * py + bx * ay - by * ax) / norm
            if d > best:
                best, at = d, i
        if best > tol and at > 0:
            keep[at] = True
            stack.append((a, at))
            stack.append((at, b))
    return [p for p, k in zip(points, keep) if k]


def area(ring):
    s = 0.0
    for (x1, y1), (x2, y2) in zip(ring, ring[1:] + ring[:1]):
        s += x1 * y2 - x2 * y1
    return abs(s) / 2


def inside(points):
    return not (max(y for _, y in points) < SOUTH or min(y for _, y in points) > NORTH)


def path(lines, closed):
    """Relative moves and lines, to two decimals: shortest in text"""
    out = []
    for pts in lines:
        cmd, cx, cy = [], 0, 0
        first = True
        for x, y in pts:
            x, y = round(x * 100), round(-y * 100)
            if first:
                cmd.append(f"M{x / 100:g} {y / 100:g}")
                first = False
            elif (x, y) == (cx, cy):
                continue
            else:
                cmd.append(f"l{(x - cx) / 100:g} {(y - cy) / 100:g}")
            cx, cy = x, y
        out.append("".join(cmd) + ("z" if closed else ""))
    return "".join(out)


def rings(features, min_area):
    out = []
    for f in features:
        g = f["geometry"]
        polys = [g["coordinates"]] if g["type"] == "Polygon" else g["coordinates"]
        for poly in polys:
            for ring in poly:
                pts = [(float(x), float(y)) for x, y in ring]
                if pts and pts[0] == pts[-1]:
                    pts = pts[:-1]
                if len(pts) < 3 or not inside(pts) or area(pts) < min_area:
                    continue
                pts = simplify(pts + [pts[0]], TOLERANCE)[:-1]
                if len(pts) >= 3:
                    out.append(pts)
    return out


def lines(features):
    out = []
    for f in features:
        g = f["geometry"]
        parts = [g["coordinates"]] if g["type"] == "LineString" else g["coordinates"]
        for part in parts:
            pts = [(float(x), float(y)) for x, y in part]
            if len(pts) >= 2 and inside(pts):
                out.append(simplify(pts, TOLERANCE))
    return out


def main():
    src, out = sys.argv[1], sys.argv[2]
    load = lambda name: json.load(open(os.path.join(src, name + ".geojson")))["features"]
    land = rings(load("ne_50m_land"), MIN_ISLAND)
    lakes = rings(load("ne_50m_lakes"), MIN_LAKE)
    borders = lines(load("ne_50m_admin_0_boundary_lines_land"))
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="-180 {n:g} 360 {h:g}" preserveAspectRatio="none">\n'
        "<!-- The world from Natural Earth's 1:50m land, lakes and country borders\n"
        "     (public domain, naturalearthdata.com), simplified, in plate carree: x is\n"
        "     longitude and y minus latitude, {north:g}N to {south:g}S. The time zone map draws\n"
        "     each layer as a mask in its own colour. scripts/generate_world_map.py makes it. -->\n"
        '<path id="land" fill="#000" fill-rule="evenodd" d="{land}"/>\n'
        '<path id="lakes" fill="#000" d="{lakes}"/>\n'
        '<path id="borders" fill="none" stroke="#000" stroke-width="0.25" stroke-linejoin="round" d="{borders}"/>\n'
        "</svg>\n"
    ).format(n=-NORTH, h=NORTH - SOUTH, north=NORTH, south=-SOUTH,
             land=path(land, True), lakes=path(lakes, True), borders=path(borders, False))
    open(out, "w").write(svg)
    print(f"land {len(land)} rings, lakes {len(lakes)}, borders {len(borders)} lines; {len(svg)} bytes")


main()
