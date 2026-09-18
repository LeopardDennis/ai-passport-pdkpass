<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Images

Store reusable source images and generated display assets here.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Circuit geometry

`circuits/` stores four GeoJSON sources retrieved on 2026-09-18 from
[bacinger/f1-circuits](https://github.com/bacinger/f1-circuits), under the
MIT license retained in [circuits/LICENSE.txt](circuits/LICENSE.txt).
These are coordinate data, not downloaded map artwork.

| Local file | Upstream source |
| --- | --- |
| [portimao.geojson](circuits/portimao.geojson) | [pt-2008.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/pt-2008.geojson) |
| [istanbul.geojson](circuits/istanbul.geojson) | [tr-2005.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/tr-2005.geojson) |
| [sakhir.geojson](circuits/sakhir.geojson) | [bh-2002.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/bh-2002.geojson) |
| [jeddah.geojson](circuits/jeddah.geojson) | [sa-2021.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/sa-2021.geojson) |

`tools/check_circuit_assets.py` projects longitude using mean-latitude cosine,
inverts latitude for screen coordinates, resamples the closed line into 48
equal-distance segments and fits it proportionally inside 192 × 61 with a
three-pixel margin. It checks the 49 embedded byte-coordinate pairs per circuit
in `main/pdkpass_tracks.c`. Firmware loads neither GeoJSON nor network geometry.
The existing UI performs its own proportional fit to the visible card.
Source filenames describe circuit identity/history, not supported season years.
