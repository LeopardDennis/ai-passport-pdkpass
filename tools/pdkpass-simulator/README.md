<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PDKPASS macOS simulator

This native Mac simulator runs the production PDKPASS LVGL screens, navigation
state machine, circuit outlines, themes, calendar, standings, and result layout.
It scales the device's exact 240 × 320 RGB565 framebuffer with nearest-neighbour
rendering instead of recreating the interface as a web mockup.

The simulator defaults to the offline 2026 calendar and standings snapshot.
Pass `--year 2027 --race 1` for the 24-round built-in 2027 calendar, with pending
session times and no assumed standings. Result caches are separated by year.
This option is for preview/testing; firmware selects by its valid clock.
When
a historical results page is opened while the simulator is online, it retrieves
that round's real session classifications from OpenF1 and caches completed
podiums in the user's macOS cache directory. Network number keys exercise the
firmware's connection states; only the online state permits result sync.

## Requirements

- macOS
- Xcode Command Line Tools
- CMake 3.16 or later
- the repository's generated `managed_components/` directory
- an internet connection when loading uncached historical results

If `managed_components/` is absent, run one normal ESP-IDF configure or build
first so the locked LVGL dependency is available.

## Build and run

From the repository root:

```bash
./tools/pdkpass-simulator/run.sh
```

If the executable bit was not preserved when the repository was copied, use
`bash tools/pdkpass-simulator/run.sh`. The equivalent manual commands are:

```bash
cmake -S tools/pdkpass-simulator -B build/pdkpass-simulator
cmake --build build/pdkpass-simulator -j
./build/pdkpass-simulator/pdkpass-simulator
```

The default home screen previews round 13. Use another round as the automatic
home selection with, for example:

```bash
./tools/pdkpass-simulator/run.sh --race 1
```

## Controls

Battery is simulated (88 by default), not the Mac or device battery. Use
`--battery 15` for low charge, `--battery 0` for empty, or `--battery -1` for an
unknown reading. The production icon shows inset digits with no percent sign
or BAT label; unknown readings show only the outline.
Network text, date and battery are vertically centered in the colored status
area, excluding the black border and drop shadow. On hardware the existing
worker still supplies real battery readings; the UI does not poll the sensor.

After building, run `python3 tools/pdkpass-simulator/test_status.py` (requires
Pillow) to check pixel centering, battery states and invalid preview arguments.

| Mac input | Device action |
| --- | --- |
| Up / Down | Browse previous / next race on home; scroll on other screens |
| Hold Up / Down for 0.65 seconds on home | Driver standings / calendar |
| Return or Space | OK |
| Hold Return for 0.65 seconds | Back |
| Escape | Back |
| `1` / `2` / `3` / `4` / `5` | Setup / connecting / time sync / online / offline |
| `S` or Command-S | Save the current 240 × 320 screen as PNG |

For a headless render suitable for a smoke test:

Use `--page home|calendar|standings|track|results` with `--screenshot` to render
a specific production page. Network views and result syncing take precedence.

Add `--network-view menu|retry|setup|confirm` to capture the production network
screens. Radio operations are simulated; validate real scan/timeout behavior on
hardware. Setup screenshots use example credentials, not device secrets.

```bash
./build/pdkpass-simulator/pdkpass-simulator \
  --race 13 --screenshot /tmp/pdkpass-simulator.png
```

To verify and capture a real historical FP1 classification without opening a
window:

```bash
./build/pdkpass-simulator/pdkpass-simulator --race 1 --sync-results \
  --screenshot /tmp/pdkpass-australia-fp1.png
```

The simulator uses the LVGL pool size from `sdkconfig.defaults`. After building,
run `ctest --test-dir build/pdkpass-simulator --output-on-failure` to check startup,
setup, standings, calendar, results, battery states and wake-up within that pool.
This does not measure the device system heap or Wi-Fi/TLS memory.
