"""Pixel regression checks against the production LVGL simulator (requires Pillow)."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get(
    "PDKPASS_SIMULATOR_BINARY",
    ROOT / "build/pdkpass-simulator/pdkpass-simulator"))


class StatusPixels(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="pdkpass-status-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.frames = {}
        for soc in (-1, 0, 1, 15, 19, 20, 21, 50, 88, 100):
            path = Path(cls.directory.name) / f"battery-{soc}.png"
            subprocess.run([str(BINARY), "--race", "13", "--battery", str(soc),
                            "--screenshot", str(path)], check=True, capture_output=True)
            with Image.open(path) as source:
                cls.frames[soc] = source.convert("RGB")

    def test_gray_area_center(self):
        image = self.frames[88]
        # The larger status text now reaches x=170; sample the clear gap.
        gray = image.getpixel((192, 63))
        rows = [y for y in range(52, 79) if image.getpixel((192, y)) == gray]
        self.assertEqual((min(rows), max(rows)), (55, 71))
        for name, left, right in (("network", 38, 84), ("date", 110, 149),
                                  ("battery", 198, 225)):
            with self.subTest(element=name):
                ink = [y for y in rows if any(
                    min(image.getpixel((x, y))) > 180 for x in range(left, right))]
                self.assertEqual(min(ink) - 55, 71 - max(ink))

    def test_home_hint_has_equal_side_margins(self):
        image = self.frames[88]
        # Ticket border occupies x=8..10 and x=229..231. Measure the visible
        # glyphs, since the font's ink does not start at its label origin.
        ink_x = [x for y in range(292, 303) for x in range(11, 229)
                 if max(image.getpixel((x, y))) < 100]
        self.assertTrue(ink_x)
        self.assertEqual(min(ink_x) - 11, 228 - max(ink_x))

    def test_every_bundled_race_home_text_fits(self):
        def text_bounds(image, color, box):
            left, top, right, bottom = box
            pixels = [(x, y) for y in range(top, bottom)
                      for x in range(left, right)
                      if image.getpixel((x, y)) == color]
            self.assertTrue(pixels)
            return (min(x for x, _ in pixels), min(y for _, y in pixels),
                    max(x for x, _ in pixels), max(y for _, y in pixels))

        for year, count in ((2026, 23), (2027, 24)):
            for race in range(1, count + 1):
                with self.subTest(year=year, race=race):
                    path = Path(self.directory.name) / f"{year}-r{race:02}.png"
                    subprocess.run([str(BINARY), "--year", str(year),
                                    "--race", str(race), "--screenshot", str(path)],
                                   check=True, capture_output=True)
                    with Image.open(path) as source:
                        image = source.convert("RGB")
                    title = text_bounds(image, (246, 246, 238),
                                        (12, 122, 228, 158))
                    circuit = text_bounds(image, (255, 255, 255),
                                          (12, 157, 228, 182))
                    date = text_bounds(image, (246, 246, 238),
                                       (12, 182, 228, 205))
                    self.assertGreaterEqual(title[3] - title[1] + 1, 19)
                    self.assertGreaterEqual(circuit[1] - title[3] - 1, 9)
                    self.assertLessEqual(circuit[2] - circuit[0] + 1, 190)
                    for bounds in (title, circuit, date):
                        self.assertGreaterEqual(bounds[0], 16)
                        self.assertLessEqual(bounds[2], 223)

    def test_every_bundled_track_title_fits(self):
        for year, count in ((2026, 23), (2027, 24)):
            for race in range(1, count + 1):
                with self.subTest(year=year, race=race):
                    path = Path(self.directory.name) / f"track-{year}-r{race:02}.png"
                    subprocess.run([str(BINARY), "--year", str(year),
                                    "--race", str(race), "--page", "track",
                                    "--screenshot", str(path)],
                                   check=True, capture_output=True)
                    with Image.open(path) as source:
                        image = source.convert("RGB")
                    ink = [(x, y) for y in range(11, 40)
                           for x in range(12, 228)
                           if image.getpixel((x, y)) == (16, 32, 41)]
                    self.assertTrue(ink)
                    left, right = min(x for x, _ in ink), max(x for x, _ in ink)
                    top, bottom = min(y for _, y in ink), max(y for _, y in ink)
                    self.assertGreaterEqual(left, 16)
                    self.assertLessEqual(right, 223)
                    self.assertGreaterEqual(top, 12)
                    self.assertLessEqual(bottom, 38)
                    self.assertEqual(bottom - top + 1, 16)
                    pixels = set(ink)
                    dots = []
                    while pixels:
                        start = pixels.pop()
                        component = {start}
                        pending = [start]
                        while pending:
                            x, y = pending.pop()
                            for dx in (-1, 0, 1):
                                for dy in (-1, 0, 1):
                                    neighbor = (x + dx, y + dy)
                                    if neighbor in pixels:
                                        pixels.remove(neighbor)
                                        component.add(neighbor)
                                        pending.append(neighbor)
                        if len(component) == 16:
                            xs = {x for x, _ in component}
                            ys = {y for _, y in component}
                            if len(xs) == len(ys) == 4:
                                dots.append((min(ys), max(ys)))
                    self.assertEqual(dots, [(23, 26)])

    def test_every_bundled_result_title_matches_track_height(self):
        for year, count in ((2026, 23), (2027, 24)):
            for race in range(1, count + 1):
                with self.subTest(year=year, race=race):
                    path = Path(self.directory.name) / f"result-{year}-r{race:02}.png"
                    subprocess.run([str(BINARY), "--year", str(year),
                                    "--race", str(race), "--page", "results",
                                    "--screenshot", str(path)],
                                   check=True, capture_output=True)
                    with Image.open(path) as source:
                        image = source.convert("RGB")
                    ink = [(x, y) for y in range(11, 40)
                           for x in range(12, 228)
                           if image.getpixel((x, y)) == (16, 32, 41)]
                    self.assertTrue(ink)
                    left, right = min(x for x, _ in ink), max(x for x, _ in ink)
                    top, bottom = min(y for _, y in ink), max(y for _, y in ink)
                    self.assertGreaterEqual(left, 16)
                    self.assertLessEqual(right, 223)
                    self.assertEqual(bottom - top + 1, 16)

    def test_red_at_or_below_twenty(self):
        for soc, image in self.frames.items():
            with self.subTest(soc=soc):
                red = sum(r > g + 50 and r > b + 50
                          for r, g, b in image.crop((199, 58, 222, 69)).getdata())
                self.assertEqual(red > 0, 0 <= soc <= 20)

    def test_battery_changes_stay_in_icon(self):
        baseline = self.frames[88]
        for soc, image in self.frames.items():
            with self.subTest(soc=soc):
                for y in range(52, 75):
                    for x in range(8, 232):
                        if not (199 <= x < 225 and 58 <= y < 69):
                            self.assertEqual(image.getpixel((x, y)), baseline.getpixel((x, y)))
        crops = {image.crop((199, 58, 222, 69)).tobytes()
                 for image in self.frames.values()}
        self.assertEqual(len(crops), len(self.frames))

    def test_invalid_battery_arguments(self):
        for value in ("101", "-2", "text", "15oops", ""):
            with self.subTest(value=value):
                result = subprocess.run([str(BINARY), "--battery", value],
                                        capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
