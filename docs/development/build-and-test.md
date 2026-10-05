<p align="right">
  <a href="build-and-test.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Build and Test

Use ESP-IDF 5.5.3. On a clean machine or when the toolchain is missing, follow
the [environment bootstrap](environment-setup.md) first.

> Prefer `./tools/validate.sh --firmware` for firmware builds and flash its
> verified `build/FoloToy-AI-Passport-full.bin` at offset `0x0` only when the
> target is blank or the merged byte range ends before protected `cardid`.
> On a provisioned device, prefer mini-program install or segmented
> `idf.py flash`. Treat
> `idf.py build` and `idf.py flash` as incremental development commands, not the
> default delivery path.

PDKPASS data caches use the 64 KB `pdk_cache` NVS partition at `0x310000`.
The 24 KB default `nvs` partition retains Wi-Fi profiles and small system data.
The new partition starts after the factory application's fixed 3 MB range and
ends before protected `cardid`. It is initialized at runtime, with no packaged
cache payload. Install the matching partition table when moving from firmware
that predates this partition; an app-only update with an old table cannot use it.
Cache initialization preserves compatible data/settings in both partitions
across upgrades, downgrades and restarts; it no longer reads or writes the legacy
`pdk_meta:image` fingerprint. Service snapshot validation and per-namespace
invalidation remain in place. `ESP_ERR_NVS_NO_FREE_PAGES` or
`ESP_ERR_NVS_NEW_VERSION_FOUND` recovers only the affected partition. Other
initialization failures block cache access and stop startup without erasing data.
Factory `cardid` and permanent `recovery` are never erased.
Use an NVS-preserving installation to retain Wi-Fi/settings and cached data:
mini-program application updates retain these regions, whereas writing a raw
merged image at 0x0 may overwrite default NVS independently of startup policy.
An application-only USB update must target 0x10000 with the matching partition
table already installed and without whole-chip erase.

The BSP button host test runs the resolved, pinned `espressif/button` source with
fake peripherals. A fresh checkout without Managed Components skips this test
in `--static`; the firmware gate requires and reruns it after dependency
resolution, so complete validation always exercises the actual gesture engine.

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version             # must report ESP-IDF v5.5.3
./tools/validate.sh --firmware # preferred: build and verify merged 0x0 image
idf.py set-target esp32c3     # fresh checkout or changed target
idf.py build                  # optional incremental application build
idf.py flash monitor          # optional incremental application flash
idf.py fullclean              # remove stale generated build state only
```

`idf.py fullclean` does not fully synchronize an existing `sdkconfig` with
changed defaults. Preserve intentional local settings, then run
`idf.py set-target esp32c3` when the target or tracked defaults must be
regenerated.

The tracked `dependencies.lock` pins Managed Component resolution. After changing an `idf_component.yml`, regenerate the lock with ESP-IDF 5.5.3, review version changes, and commit it with the manifest. An ordinary build must not leave an unexplained lock-file diff.

Firmware validation uses a fresh temporary build directory and an isolated `sdkconfig` generated from the tracked defaults. It does not consume or overwrite a developer's root `sdkconfig`, and it copies only the verified merged image to `build/FoloToy-AI-Passport-full.bin`. The gate also enforces the [mini-program BLE compatibility contract](ble-recovery-compatibility.md): protected partition addresses, application size, partition-table MD5, absence of protected payload data, and the Recovery bootloader hook.

The baseline also has a hardware-independent logic test:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

Use the unified validation entry point:

```bash
./tools/validate.sh --static    # repository checks, workflows, links, secrets, host tests
./tools/validate.sh --firmware  # build, merge-bin, offsets, and BLE compatibility
./tools/validate.sh             # complete gate; requires an activated ESP-IDF environment
```

CI calls the same script. Fix the shared script or environment if local and CI behavior differs; do not duplicate command sequences in workflows.

Hardware-affecting changes must also run the applicable on-device checklist in the hardware guide. Report compilation separately from physical-device validation.

## macOS device log capture

Release firmware has no charging sampler or USB screenshot debug endpoint.
Serial output contains necessary warnings and errors only. The collector below
can record boot or networking failures without flashing or extra battery reads.

On the test Mac, check out the same commit as the firmware being flashed. The
log collector uses only Python 3's standard library; ESP-IDF and extra Python
packages are not needed for capture. After flashing and Wi-Fi setup, close the
flasher and other serial monitors, then keep the USB data cable connected:

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300
# If auto-detection finds no unique /dev/cu.usbmodem* port:
python3 tools/device-test/serial_capture.py --port /dev/cu.usbmodemXXXX --seconds 300
```

Start capture before restarting the board once, so the boot and reconnection
logs are included. The collector never flashes, erases, or sends commands to
the device. It saves a private raw log under `/tmp/pdkpass-device-logs/` and
prints counts for observed warning and error messages. Those counts
cannot prove display, button, sound, battery, or timing behavior; follow the
hardware guide's on-device checklist. Inspect and redact network or personal
details before sharing a raw log.

Never upload the app-only `build/FoloToy-AI-Passport.bin` to the community. Only
the validated `build/FoloToy-AI-Passport-full.bin` contains the structure the
mini-program can inspect and transform safely.
