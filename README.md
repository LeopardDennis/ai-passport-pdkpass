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
- Bundles the 2026 and 2027 calendars and distinct circuit outlines for offline
  first use.
- Downloads the current season and standings when online, then keeps the latest
  valid copy available offline.
- Caches podiums for FP1, FP2, FP3, sprint qualifying, sprint, qualifying, and
  the Grand Prix as results become available.
- Dims and turns off the backlight after inactivity.

## Controls

| Screen | UP / DOWN | Hold UP / DOWN | OK | Hold OK |
| --- | --- | --- | --- | --- |
| Home | Previous / next race | Driver / team points | Displayed race details | Network menu |
| Network menu | Select action | — | Confirm | Home |
| Retry | — | — | Back after failure | Cancel |
| Setup QR / details | — | — | Show details / return to QR | Close hotspot |
| Driver / team standings | Scroll rankings | — | Refresh both points tables | Home |
| Race details | Previous / next race | — | Session results | Back |
| Session results | Previous / next session | — | Refresh this round | Race details |

## No app required

No phone app is required. Hold OK on the home screen to open `NETWORK`, select
`WI-FI SETUP`, and confirm if already connected. The device scans nearby
networks, then opens a temporary hotspot and displays its QR code. Press OK
to show the backup screen with the hotspot name, password and `192.168.9.1`.

Each opening generates a new eight-character WPA2 password using uppercase
letters and digits, without `I`, `O`, `0` or `1`.

1. Scan the QR code with a phone and join the device hotspot, or use the backup
   details to connect manually.
2. Open `http://192.168.9.1` in the phone browser.
3. Choose a real nearby 2.4 GHz network or enter its name manually, enter the
   password, then press **Connect**.

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

To add a network, reopen Wi-Fi setup. Saving the same Wi-Fi name updates its password; a sixth
name replaces the least recently connected network. Only the current versioned
network list is loaded; older single-network credentials are not imported. No
phone app is required. Writing the merged firmware from `0x0` in the browser
flasher overwrites NVS, so Wi-Fi must be configured again after that installation.

The top status changes through `SETUP`, `WIFI...`, `TIME...`, and `WIFI OK`.
`WIFI OK` indicates connectivity, not that calendar or results data is current.
The network menu shows separate Beijing dates for successful `CAL SYNC`,
`RESULTS`, `DRIVERS` and `TEAMS` checks in the active season. Manual and automatic
checks update the matching date after complete responses are validated and any
changed data is saved. An unchanged response also counts as a successful check;
partial points updates record only the table that succeeded. Points checks do
not update the calendar date. Existing calendar/results dates are retained,
and each data set writes its date at most once per Beijing day. A date from another season is not shown
as current: the menu says `CAL 2027 NO SYNC` or `RESULT 2027 NO SYNC` until that
season is updated. An active-season cache without a recorded date says
`CACHE DATE?`; `NEVER` means neither cached data nor a recorded update. After time
synchronization, the device keeps counting locally. It rechecks the
season at Beijing midnight and at the current round's switch boundary, as well
as after the network becomes available. Each online round remains current until
its recorded race end, then the dashboard advances to the following round. The
bundled offline fallback uses a four-hour window from the scheduled race start.
After the final round it displays `SEASON COMPLETE`.

## Session reminders

Reminders default to on. In `NETWORK`, select `ALERTS: ON/OFF` and press OK to
change the saved setting. Every scheduled FP1, FP2, FP3, sprint qualifying,
sprint, qualifying and race session rings once, ten minutes before its start.
Missing or cancelled sessions do not ring. Schedule updates change the deadline.
The same switch controls the cue when a session result is first saved; button
cues remain available when `ALERTS` is off.

The approved three-second melody plays at 80%; button cues remain at 50%.
Audio initialization/open failures retry on a new cue after at least 60 seconds;
failed cues are discarded rather than replayed later.
A notice wakes the display for up to 15 seconds, showing the round, session and
start time. Any key silences and dismisses it without navigating. It then
restores the previous page and returns to sleep if the display was already dark.

The existing background session sync caches exact API start times locally.
No results-page visit is needed. A valid synchronized clock and cached schedule
allow reminders while offline or in screen-off/light sleep. After a full power
cycle, time must be synchronized again; a fully powered-off device cannot ring.
Initial API access failures, including live-session authentication restrictions,
can prevent the schedule from being downloaded. There is no guessed-time fallback.

A successful sync inside the ten-minute window produces one catch-up reminder;
none is issued after the session starts. Delivered session IDs persist before
ringing to prevent duplicates after reconnect/restart. If saving fails, delivery
waits and retries after five seconds. A power failure between that save and the
actual sound can skip that reminder. Timers use the existing worker; there is
no new task or per-second network polling. Real-device loudness, wake-up, power
consumption and simultaneous TLS/audio headroom still require testing.

## Power saving

The display dims after 30 seconds and turns its backlight off after 90 seconds.
While dark, the LCD controller enters sleep and drawing/invalidation pause; the clock and Beijing race
switch timers continue. Manual-sync polling pauses while dark and reads the
latest state immediately after a successful key or reminder wake. The first key
press wakes the display without navigating.
If you browse other rounds on the home screen, the displayed round returns to
the current weekend when the screen turns off after 90 seconds of inactivity.
ADC keys are still scanned every 20 ms, rather than relying on unverified GPIO
wake thresholds. No deep sleep is used.
The battery worker reads the fuel gauge once a minute while the display is lit.
It stops polling while the display is off and reads once immediately after a
key wakes the screen; unchanged readings do not redraw the battery icon.

The CPU stays at 160 MHz while the screen is lit, and may drop to 40 MHz and
enter automatic light sleep while dark. Setup/connection work and an attached
USB console prevent light sleep. The LVGL clock reads monotonic time instead of
requiring a periodic 5 ms tick interrupt.

After clock synchronization, Wi-Fi is checked for idleness every 30 seconds.
When both data services have no work within the next minute and no HTTP
transaction is active, the radio switches off. Their actual deadlines (daily
updates, post-session results or retry backoff) trigger reconnection. Historical
backfill and imminent work keep the link up to avoid repeated handshakes.
`WIFI OFF` after a successful update is therefore normal. Opening uncached race
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
cached. Results navigation only includes sessions scheduled for that round:
normal weekends skip sprint qualifying/sprint, and sprint weekends skip FP2/FP3.
Before session metadata arrives, the bundled calendar supplies the weekend
format; downloaded metadata takes precedence. Scheduled sessions remain
visible while results are pending, and cancelled sessions retain their notice.
Press OK on results to recheck only the displayed session, including cached
results that may have been corrected. Other sessions remain unchanged. The footer reports the
outcome; existing results remain visible if a request fails. Repeated manual
requests have a 60-second cooldown. Each accepted refresh has a 120-second
total deadline, including shared HTTP waits; expiration shows `SYNC TIMEOUT`.
If saving downloaded results fails, the cache stays pending and retries locally
after 60 seconds, including while offline. A busy shared transaction delays
that retry until available; successful saving stops the local retry. Manual
refresh still reports its save failure. Pending changes can be lost if power
is removed before a successful save.
Opening a result or waking its page prioritizes that round across successive
session downloads. Failed requests for that round retry after five minutes,
even when background backfill has a longer delay. Selecting another round
moves the priority; already cached podiums are not downloaded again.
Manual priority expires 15 minutes after the last request, so an unattended
historical round returns to daily result retry instead of keeping five-minute
retries indefinitely. Current-weekend automatic result checks remain active.

Historical session classifications and driver metadata come from the unofficial
[OpenF1 API](https://openf1.org/docs/). PDKPASS uses the unauthenticated
historical endpoint and does not embed an OpenF1 account, password, or access
token.

## Season data and offline behaviour

The first-use calendar is an offline snapshot captured on 31 August 2026 from the
[official 2026 Formula 1 calendar](https://www.formula1.com/en/racing/2026).
Displayed session times are converted to China Standard Time (UTC+8).
Driver standings have no bundled default points: without a downloaded cache,
the page shows `CONNECT TO UPDATE` and `DRIVER DATA PENDING`. Successful syncs
are saved for offline use across reboots of the same firmware image. Installing
a different image clears downloaded calendar, driver/team standings, results
and their sync dates before services start, then downloads fresh data. Old
default-NVS caches are not imported. Wi-Fi, clock and reminder settings remain
intact.

After the first successful connection, PDKPASS downloads and stores the Grand
Prix calendar for the current Beijing-time year and the latest published
Jolpica championship standings. With a clock synchronized during the current boot, Beijing New Year selects a newer
bundled season even without Wi-Fi or successful HTTPS. The worker also checks
at Beijing midnight while offline. A same-year/newer downloaded cache wins over
the seed, and an unavailable future year keeps the last working season.
Flash contains both bundled calendars; RAM and downloaded caches keep one
active season (up to 24 races, 24 drivers and seven session types).

The 2027 seed follows the
[official 16 September announcement](https://corp.formula1.com/2027-calendar-announced-with-10-sprint-events/):
24 rounds and 10 Sprint weekends. Dates are venue-local; unpublished session
times show `TIME TBD`, with no invented times, laps, drivers or standings.
Until session metadata arrives, automatic round selection uses midnight after
the last published date in the venue's time zone, not a claimed race-end time.
Downloaded session end times replace this date-only boundary. Istanbul remains
subject to FIA circuit homologation. Existing circuit colors remain unchanged.
OpenF1 calendar/results arrays are processed item by item. Jolpica standings
are downloaded in pages of four drivers with a 4 KB response cap and paced
requests. All pages must agree on the season, round and total; incomplete or
invalid responses retain the previous points and date. Device memory and TLS
behaviour still need validation.

The most recent valid time, accepted season, standings, and downloaded podiums
remain available offline. After a long powered-off period, reconnect to refresh
them. Championship points and their driver/team metadata come exclusively from
[Jolpica](https://github.com/jolpica/jolpica-f1/blob/main/docs/endpoints/driverStandings.md).
Calendar and session results continue to use [OpenF1](https://openf1.org/docs/).
An OpenF1 calendar failure does not block same-season Jolpica updates; each
provider has independent rate-limit backoff. Standings publish only after the
complete snapshot is validated and saved. Drivers with multiple constructors
in the season show `MULTIPLE TEAMS`, since their order does not identify the
current team.

Hold DOWN on home to open `TEAM POINTS`; hold UP for driver standings.
UP/DOWN scroll through rankings; hold OK to return home. Short OK refreshes both
driver and team standings without reloading the calendar. Existing tables stay
visible during the request; the footer reports the outcome. Repeated manual
requests have a 60-second cooldown. Each accepted refresh has a 120-second
total deadline, including shared HTTP waits; expiration shows `SYNC TIMEOUT`.
The home DOWN hold no longer opens the calendar list; short UP/DOWN still browse races.
Team points come directly from Jolpica constructor standings, including provider
adjustments, rather than a sum of driver scores. They refresh automatically with
the season worker and have an independent offline cache for up to 16 teams.
Until the first complete download, the page shows pending data. New Year clears
the visible old-season teams until current-season data arrives; driver points,
team points and calendar failures do not discard one another's valid caches.

### Refresh resources

Repeated manual sync shows the actual cooldown in seconds. Each serialized
refresh reuses one same-origin HTTPS client and releases TLS before cache saves
or when the refresh ends. PDKPASS audio allocates TX/DAC only, keeping recording
available to other BSP consumers through the full-duplex initializer.

Private store, parser, standings, portal and network-page implementation groups live in
`.inc` files included by their owning service; their workers and locks retain
one owner without adding tasks or shared mutable interfaces.

### Year-independent circuit catalog

The offline catalog contains 27 circuits, including Portimão, Istanbul Park,
Sakhir and Jeddah. Stable circuit IDs resolve display names and aliases to the
same outline, length, accent and background across seasons. Race dates, rounds,
session results and lap counts remain event data. A new season does not need
the preceding season's cache to recover circuit details. Unknown lap counts
display as `-- LAPS` without hiding a known circuit length.

These are the stored circuit layouts, not a guarantee of future homologation.
A materially changed layout should receive a separate layout identity; do not
overwrite historical geometry merely because the calendar year changes.
There is no manual season-selection page; selection follows the valid clock.

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

The four added outlines use the MIT-licensed
[bacinger/f1-circuits](https://github.com/bacinger/f1-circuits) GeoJSON data.
Sources and the copyright notice are stored in
[`assets/images/circuits/`](assets/images/circuits/).
Run `python3 tools/check_circuit_assets.py` to verify the embedded coordinates
against these offline sources. Portimão (4.653 km) and Istanbul (5.338 km) use
their documented existing GP layouts, not an assumed revised 2027 layout.

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

## Reliability

Release firmware keeps necessary warnings and errors only. It contains no
charging-diagnostic sampling, I2C scanner, USB screenshot task, progress logs
or heap sampling. Battery display still uses the gauge's measured SOC; charging
and SOC parameters are unchanged.

Standings are checked again 30 minutes after the recorded race end; failed
synchronization retries after five minutes. The displayed standings date refers
to Jolpica's corresponding round date (converted to Beijing time), not the
download date. Published Sprint points may precede that round's scheduled race. Current
weekends take priority over historical backfill. API availability and rate limits
can delay publication beyond these local retry intervals.

HTTP, JSON parsing and allocation failures retain compact error reports with
stage, status and error code. Logs contain no request URLs, response bodies,
Wi-Fi passwords or other secrets.

Setup uses a new random password each time it starts. Wait for an in-flight
connection test to finish before submitting another network. `NTP ERR` means
Wi-Fi connected but time synchronization is still retrying. A clock successfully
synchronized during this boot remains usable across reconnects; a restored NVS
time is only an offline estimate and does not account for power-off duration.
The home status prefixes its date with `~`; this estimate does not trigger
automatic round or season changes until time synchronization succeeds.

Use the native simulator for previews and photographs for device evidence.
Screen-off stops the UI idle timer; automatic light sleep is
configured, but ADC-button responsiveness and actual battery current still
require board validation.
