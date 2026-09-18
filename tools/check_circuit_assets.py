"""Verify embedded circuit outlines against the vendored GeoJSON sources."""
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def resample(source):
    feature = source.get("features", [source])[0]
    coordinates = feature["geometry"]["coordinates"]
    latitude = math.radians(sum(p[1] for p in coordinates) / len(coordinates))
    points = [(p[0] * math.cos(latitude), -p[1]) for p in coordinates]
    if points[0] != points[-1]:
        points.append(points[0])
    distances = [0]
    for a, b in zip(points, points[1:]):
        distances.append(distances[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    xs, ys = zip(*points)
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    scale = min(185 / (max(xs) - min(xs)), 54 / (max(ys) - min(ys)))
    result, segment = [], 1
    for i in range(48):
        distance = distances[-1] * i / 48
        while distances[segment] < distance:
            segment += 1
        t = (distance - distances[segment - 1]) / (distances[segment] - distances[segment - 1])
        a, b = points[segment - 1], points[segment]
        x, y = (a[k] + (b[k] - a[k]) * t for k in range(2))
        result.extend((math.floor(95.5 + (x - cx) * scale + 0.5),
                       math.floor(30 + (y - cy) * scale + 0.5)))
    return result + result[:2]


if __name__ == "__main__":
    embedded = (ROOT / "main/pdkpass_tracks.c").read_text()
    for name in ("portimao", "istanbul", "sakhir", "jeddah"):
        source = json.loads((ROOT / f"assets/images/circuits/{name}.geojson").read_text())
        body = re.search(r"static const uint8_t s_" + name + r"\[\] = \{([^}]+)", embedded)[1]
        assert resample(source) == [int(n) for n in re.findall(r"\d+", body)], name
    print("Vendored circuit geometry: PASS (4 outlines, 48 segments each)")
