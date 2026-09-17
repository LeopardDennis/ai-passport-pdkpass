<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PDKPASS

<p align="center">
  <strong>Your Formula 1 weekend pass for FoloToy AI Passport.</strong><br>
  Race calendar · driver standings · circuit details · session podiums
</p>

> **Project status:** the firmware build and host tests pass, and the native
> simulator runs the production interface. First on-device validation is still
> pending.

<table>
  <tr>
    <td><img src="docs/assets/pdkpass/home-r01.png" alt="PDKPASS Australia home screen"></td>
    <td><img src="docs/assets/pdkpass/home-r13.png" alt="PDKPASS Italy home screen"></td>
    <td><img src="docs/assets/pdkpass/home-r23.png" alt="PDKPASS Abu Dhabi home screen"></td>
  </tr>
  <tr>
    <td align="center">Australia</td>
    <td align="center">Italy</td>
    <td align="center">Abu Dhabi</td>
  </tr>
</table>

<p align="center"><sub>Captured directly from the production UI in the native simulator—not design mockups.</sub></p>

## Why PDKPASS

PDKPASS turns AI Passport into a compact, offline-first F1 companion. Open it
to see the current or next Grand Prix, browse the season, check the driver
standings, inspect circuit details, and revisit the top three from every
supported weekend session.

- Selects the current or next round automatically using Beijing time.
- Gives every round its own colour while keeping the race-pass visual language
  consistent across the calendar, details, standings, results, setup, and
  season-end screens.
- Bundles the complete 2026 calendar and distinct circuit outlines for offline
  first use.
- Downloads the current season and standings when online, then keeps the latest
  valid copy available offline.
- Caches podiums for FP1, FP2, FP3, sprint qualifying, sprint, qualifying, and
  the Grand Prix as results become available.
- Dims and turns off the backlight after inactivity.

## Controls

| Screen | UP / DOWN | OK | Hold OK |
| --- | --- | --- | --- |
| Next race | Standings / calendar | Race details | Network menu |
| Network menu | Select action | Confirm | Home |
| Retry / setup | — | Back after retry failure | Cancel / close hotspot |
| Calendar | Select race | Race details | Home |
| Standings | Scroll drivers | — | Home |
| Race details | Previous / next race | Session results | Back |
| Session results | Previous / next session | Race details | Race details |

## No app required

No phone app is required. Hold OK on the home screen to open `NETWORK`, select
`WI-FI SETUP`, and confirm if already connected. Only this action opens the
temporary hotspot and displays its name, password and `192.168.9.1`:

Each opening generates a new eight-character WPA2 password using uppercase
letters and digits, without `I`, `O`, `0` or `1`.

1. Connect a phone to the displayed `PDKPASS-XXXX` Wi-Fi network.
2. Open `http://192.168.9.1` in the phone browser.
3. Enter a 2.4 GHz Wi-Fi name and password, then press **Connect**.

PDKPASS tests the connection before saving it. A wrong password leaves the setup
page available for another attempt without replacing saved credentials. Up to five
networks survive power-off; the last successfully connected network is tried first.
Boot and reconnection scan once and try visible saved networks, twice each
(up to 15 seconds per attempt). No profiles means no scan. Exhaustion powers
Wi-Fi off without opening a hotspot or periodically retrying. Use `RETRY WI-FI`
to start another scan; it never opens a hotspot or disrupts a healthy link.

Setup closes after three continuous minutes without a phone, or ten minutes
from opening regardless of phone reconnections or form submissions. A candidate
connection suspends the idle check but not the ten-minute cap. Obtaining an IP
and saving credentials closes setup immediately; time sync runs separately.
Hold OK to cancel retry/setup. Screen dimming and sleep continue normally.

To add a network, use the manual setup menu. Saving the same Wi-Fi name updates its password; a sixth
name replaces the least recently connected network. The single network saved by
older firmware is imported automatically. No phone app is required.

The top status changes through `SETUP`, `WIFI...`, `TIME...`, and `ONLINE`.
After time synchronization, the device keeps counting locally. It rechecks the
season at Beijing midnight and at the current round's switch boundary, as well
as after the network becomes available. Each online round remains current until
its recorded race end, then the dashboard advances to the following round. The
bundled offline fallback uses a four-hour window from the scheduled race start.
After the final round it displays `SEASON COMPLETE`.

## Power saving

The display dims after 30 seconds and turns its backlight off after 90 seconds.
While dark, drawing and display invalidation pause; the clock and Beijing race
switch timers continue. The first key press wakes the display without navigating.
ADC keys are still scanned every 20 ms, rather than relying on unverified GPIO
wake thresholds. No deep sleep is used.

The CPU stays at 160 MHz while the screen is lit, and may drop to 80 MHz and
enter automatic light sleep while dark. Setup/connection work and an attached
USB console prevent light sleep. The LVGL clock reads monotonic time instead of
requiring a periodic 5 ms tick interrupt.

After clock synchronization, Wi-Fi is checked for idleness every 30 seconds.
When both data services have no work within the next minute and no HTTP
transaction is active, the radio switches off. Their actual deadlines (daily
updates, post-session results or retry backoff) trigger reconnection. Historical
backfill and imminent work keep the link up to avoid repeated handshakes.
`OFFLINE` after a successful update is therefore normal. Opening uncached race
details/results can also wake an intentionally parked connection.

Automatic wake is allowed only after this deliberate idle shutdown. Failed
saved-network attempts, first-boot clock-sync timeout (60 seconds), or manual
cancellation stay offline until manual retry/setup or reboot. Hotspot expiry
rules remain unchanged. USB-connected measurements do not represent battery
standby; actual current, ADC response and wake reliability require device tests.

## Session results

When a session has ended, open its race details and press **OK** to browse FP1,
FP2, FP3, sprint qualifying, sprint, qualifying, and race results. PDKPASS waits
at least 30 minutes after the recorded session end, then checks for the top
three. The background worker retries at a low rate, so a free result normally
appears about 30–40 minutes after the session and remains available offline once
cached. A normal weekend reports `NO SESSION` for sprint-only slots.

Historical session classifications and driver metadata come from the unofficial
[OpenF1 API](https://openf1.org/docs/). PDKPASS uses the unauthenticated
historical endpoint and does not embed an OpenF1 account, password, or access
token.

## Season data and offline behaviour

The first-use fallback is an offline snapshot captured on 31 August 2026 from the
[official 2026 Formula 1 calendar](https://www.formula1.com/en/racing/2026) and
[official driver standings](https://www.formula1.com/en/results/2026/drivers).
Displayed session times are converted to China Standard Time (UTC+8).

After the first successful connection, PDKPASS downloads and stores the Grand
Prix calendar for the current Beijing-time year and the standings from the
latest completed race. At a year boundary it switches only after OpenF1 returns
at least one valid Grand Prix for the new year; otherwise it keeps the last
working season and tries again at the next scheduled check. The device stores
one current season at a time, including up to 24 races, 24 drivers, and the
cached podiums for all seven supported session types. The previous season cache
is replaced only after the new season has been accepted.

The most recent valid time, accepted season, standings, and downloaded podiums
remain available offline. After a long powered-off period, reconnect to refresh
them. Dynamic calendar, standings, session classifications, and driver metadata
come from the unofficial [OpenF1 API](https://openf1.org/docs/).

### Circuit-outline provenance

The compact 2026 outlines were resampled to 48 segments from the creator's
local Apex track resource set. Its metadata references OpenF1's
`circuit_info_url` and MultiViewer for most circuits, OpenStreetMap geometry for
Sepang, and the official Madring circuit map for Madring. PDKPASS embeds only
the resampled coordinates, not the source JSON, SVG, or map artwork.

OpenF1 identifies the detailed circuit information as data provided by
[MultiViewer](https://multiviewer.app/docs/); Sepang's geometry is attributed to
[OpenStreetMap contributors](https://www.openstreetmap.org/copyright). This is
an independent, non-commercial fan use. Review the relevant source terms before
any commercial redistribution.

## Try the real interface on macOS

The repository includes a native simulator that runs the same PDKPASS screens,
navigation, themes, circuit outlines, standings, and result layouts as the
firmware.

```bash
git clone https://github.com/LeopardDennis/ai-passport-pdkpass.git
cd ai-passport-pdkpass
./tools/pdkpass-simulator/run.sh
```

Use `--race 1` through `--race 23` to open another round. See the
[simulator guide](tools/pdkpass-simulator/README.md) for keyboard controls,
headless screenshots, and historical-result sync.

## Build the firmware

Use ESP-IDF 5.5.3 and run:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

Only distribute the validated `build/FoloToy-AI-Passport-full.bin` artifact.
The complete build, flashing cautions, and protected Recovery requirements are
documented in the [build and test guide](docs/development/build-and-test.md).

## Documentation

- [Documentation index](docs/INDEX.md)
- [Native simulator](tools/pdkpass-simulator/README.md)
- [Build and test](docs/development/build-and-test.md)
- [BLE and Recovery compatibility](docs/development/ble-recovery-compatibility.md)

## Licence and disclaimer

The source code is available under the [MIT Licence](LICENSE).

PDKPASS is an independent fan project and is not affiliated with or endorsed by
Formula 1, the FIA, or FoloToy. Formula 1 and related marks belong to their
respective owners.

## Reliability and debug builds

Standings are checked again 30 minutes after the recorded race end; failed
synchronization retries after five minutes. The displayed standings date refers
to the source race, not the day an old classification was downloaded. Current
weekends take priority over historical backfill. API availability and rate limits
can delay publication beyond these local retry intervals.

Setup uses a new random password each time it starts. Wait for an in-flight
connection test to finish before submitting another network. `NTP ERR` means
Wi-Fi connected but time synchronization is still retrying. A clock successfully
synchronized during this boot remains usable across reconnects; a restored NVS
time is only an offline estimate and does not account for power-off duration.

Normal firmware disables the USB screenshot worker to save resources. Enable
`CONFIG_PDKPASS_SCREENSHOT` in menuconfig for device screenshot debugging; capture
has a two-second output deadline. The native simulator remains available for
screenshots. Screen-off stops the UI idle timer, but this build does not enable
automatic Light-sleep; ADC-button wake behavior and battery current still require
board validation.
