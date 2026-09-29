<p align="right">
  <a href="device-test-handoff.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PDKPASS charging diagnosis and device testing on another Mac

Use this guide to diagnose charging first, then test a candidate firmware from `main` on another Mac. Prepare the community project and GitHub Release only after charging is resolved and device acceptance passes.

## 1. Get the code and charging diagnostic image from GitHub

Clone `https://github.com/LeopardDennis/ai-passport-pdkpass` on the new Mac, or update an existing checkout. Record the full `git rev-parse HEAD` commit ID and compare it with the commit shown on the GitHub Actions run.

In the repository, open **Actions → Build firmware → Run workflow**, select `main`, and check only **Also build read-only battery logs for charging diagnosis**. One run produces two separate downloads:

- `passport-main` contains `FoloToy-AI-Passport-full.bin` for functional acceptance after charging is resolved.
- `passport-main-battery-diagnostics` contains `FoloToy-AI-Passport-battery-diagnostics-full.bin`. It records raw battery data every 60 seconds for charging diagnosis.

Wait for both builds and artifact uploads to succeed, then download and unzip them. Running the workflow on a branch does not create a GitHub Release. Record each file's size and `shasum -a 256 filename` output. Do not mix up the two images. The `build/` directory is not in Git, so cloning alone will not provide a `.bin` file.

## 2. Diagnose charging first

Flash the battery diagnostic image first. [`CI-build-and-release.md`](CI-build-and-release.md) links the browser flasher: select the complete merged image and write it at `0x0`. Do not run `erase-flash`; preserve the device's `cardid` identity and permanent Recovery. Stop and inspect the device if the flasher reports a Flash size other than 8 MB.

After cloning the repository, capture USB serial logs with the standard-library script:

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300 --output "$HOME/Desktop/pdkpass-$(date +%Y%m%d-%H%M%S).log"
```

Keep the device connected to the computer and charging while collecting `battery_diag` lines. If you happen to observe when the charge LED turns off, tell Codex so that event can be matched to the log; voltage and raw charge trends can be analyzed without it. Increase `--seconds` if 300 seconds cannot cover the change. Unplugging that same USB cable ends serial capture; resting voltage requires a separate measurement. The diagnostic image samples more often and is not suitable for battery-life acceptance.

Before sharing the log, inspect and redact personal details, network names, or passwords. Include LED state and matching times. One `4196 mV / 96% / LED off` sample alone cannot establish whether the cell is undercharged or whether the gauge, charger, or LED behavior is responsible.

## 3. Accept all functions and prepare publication after the fix

Once charging is understood and fixed, flash the normal image and use the [device acceptance checklist](device-release-checklist.md) for boot, buttons, display, setup, networking, data, alerts, audio, power, and stability. For a live screen needed by the community listing, manually run the workflow again with **Also build charging logs and USB screenshot capture for device testing** and download `passport-main-hardware-diagnostics`. It supports `FAP_SCREENSHOT_V1`; retain the capture receipt. Never put the hotspot password or other device secrets on a public cover.

After screen capture, flash the accepted normal image again, verify its SHA-256, and perform a final boot, setup, network, and button smoke test. A real ten-minute pre-race alert and a full charge/discharge curve need longer observation and must not be reported as passed after a short session.
