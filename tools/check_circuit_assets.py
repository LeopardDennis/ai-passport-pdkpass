"""Verify embedded circuit outlines against the vendored GeoJSON sources."""
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def segment_distance(point, first, last):
    dx, dy = last[0] - first[0], last[1] - first[1]
    length_squared = dx * dx + dy * dy
    t = 0 if not length_squared else max(0, min(1,
        ((point[0] - first[0]) * dx + (point[1] - first[1]) * dy) / length_squared))
    return math.hypot(point[0] - first[0] - t * dx,
                      point[1] - first[1] - t * dy)


def display_points(source, *, landscape=False):
    feature = source.get("features", [source])[0]
    coordinates = feature["geometry"]["coordinates"]
    latitude = math.radians(sum(p[1] for p in coordinates) / len(coordinates))
    points = [(p[0] * math.cos(latitude), -p[1]) for p in coordinates]
    if landscape:
        # Quarter turn counterclockwise, keeping handedness and equal scaling.
        points = [(y, -x) for x, y in points]
    if points[0] != points[-1]:
        points.append(points[0])
    xs, ys = zip(*points)
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    scale = min(185 / (max(xs) - min(xs)), 54 / (max(ys) - min(ys)))
    return [(95.5 + (x - cx) * scale, 30 + (y - cy) * scale)
            for x, y in points]


def resample(source, *, preserve_corners=False):
    if preserve_corners:
        points = display_points(source, landscape=True)[:-1]
        # Spend the fixed point budget on bends, rather than straight lengths.
        # Retain the route origin and remove the least significant point first.
        while len(points) > 48:
            index = min(range(1, len(points)), key=lambda i: segment_distance(
                points[i], points[i - 1], points[(i + 1) % len(points)]))
            del points[index]
        assert len(points) == 48
        points.append(points[0])
        return [math.floor(value + 0.5) for point in points for value in point]
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
        expected = resample(source, preserve_corners=(name == "sakhir"))
        assert expected == [int(n) for n in re.findall(r"\d+", body)], name
        if name == "sakhir":
            outline = list(zip(expected[::2], expected[1::2]))
            # Every original coordinate must remain within one display pixel.
            error = max(min(segment_distance(point, a, b)
                            for a, b in zip(outline, outline[1:]))
                        for point in display_points(source, landscape=True))
            assert error <= 1.0, f"Sakhir lost a bend: {error:.3f}px"
    print("Vendored circuit geometry: PASS (4 outlines, 48 segments each; Sakhir shape error <= 1px)")
