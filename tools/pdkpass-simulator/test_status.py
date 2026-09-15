"""Pixel regression checks against the production LVGL simulator (requires Pillow)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "build/pdkpass-simulator/pdkpass-simulator"


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
        gray = image.getpixel((170, 63))
        rows = [y for y in range(52, 79) if image.getpixel((170, y)) == gray]
        self.assertEqual((min(rows), max(rows)), (55, 71))
        for name, left, right in (("network", 38, 84), ("date", 110, 149),
                                  ("battery", 198, 225)):
            with self.subTest(element=name):
                ink = [y for y in rows if any(
                    min(image.getpixel((x, y))) > 180 for x in range(left, right))]
                self.assertEqual(min(ink) - 55, 71 - max(ink))

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
