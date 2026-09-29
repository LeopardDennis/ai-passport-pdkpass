<p align="right">
  <a href="device-test-handoff.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PDKPASS device testing on another Mac

Use this guide to test a candidate firmware from `main` on a different macOS computer. Building and testing do not publish a community project or GitHub Release.

## 1. Get the code and two firmware images from GitHub

Clone `https://github.com/LeopardDennis/ai-passport-pdkpass` on the new Mac, or update an existing checkout. Record the full `git rev-parse HEAD` commit ID and compare it with the commit shown on the GitHub Actions run.

In the repository, open **Actions → Build firmware → Run workflow**, select `main`, and check **Also build charging logs and USB screenshot capture for device testing**. One run produces two separate downloads:

- `passport-main` contains `FoloToy-AI-Passport-full.bin` for functional acceptance and eventual release.
- `passport-main-hardware-diagnostics` contains `FoloToy-AI-Passport-hardware-diagnostics-full.bin`. It records raw battery data every 60 seconds and supports USB screen capture. Use it only for diagnosis and publishing evidence.

Wait for both builds and artifact uploads to succeed, then download and unzip them. Running the workflow on a branch does not create a GitHub Release. Record each file's size and `shasum -a 256 filename` output. Do not mix up the two images. The `build/` directory is not in Git, so cloning alone will not provide a `.bin` file.

## 2. Flash and test the normal image

Test the normal image first. [`CI-build-and-release.md`](CI-build-and-release.md) links the browser flasher: select the complete merged image and write it at `0x0`. Do not run `erase-flash`; preserve the device's `cardid` identity and permanent Recovery. Stop and inspect the device if the flasher reports a Flash size other than 8 MB.

Use the [device acceptance checklist](device-release-checklist.md) for boot, buttons, display, setup, networking, data, alerts, audio, power, and stability. Send observed results back in the chat from the new computer.

After cloning the repository, capture USB serial logs with the standard-library script:

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300 --output "$HOME/Desktop/pdkpass-$(date +%Y%m%d-%H%M%S).log"
```

Before sharing the log, inspect and redact personal details, network names, or passwords. Include the actions and screen state leading to an issue, its time, and surrounding log lines.

## 3. Diagnose charging and capture a real screen

After normal functional acceptance, flash the hardware diagnostic image if investigating charging behavior. Record when the charge LED turns off and collect `battery_diag` lines before and after that event. Its extra sampling means it cannot substitute for normal battery-life testing. The normal image does not include the USB screenshot protocol; capture a fresh device screen and its receipt for community publication while the diagnostic image is running. Never put the hotspot password or other device secrets on a public cover.

After diagnosis and screen capture, flash the accepted normal image again, verify its SHA-256, and perform a final boot, setup, network, and button smoke test. A real ten-minute pre-race alert and a full charge/discharge curve need longer observation and must not be reported as passed after a short session.
