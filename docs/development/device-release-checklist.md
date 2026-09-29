<p align="right">
  <a href="device-release-checklist.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PDKPASS device release checklist

Record the firmware filename, SHA-256, Git commit, test time, and observed result for each item. A successful build does not establish that the device passed. Leave untested items as pending.

| Area | On-device result to verify | Status |
| --- | --- | --- |
| Before flashing | USB serial is visible; physical Flash is 8 MB; use the complete merged image at `0x0` without erasing the whole chip | Pending |
| Boot | Home screen loads; no repeated restart, crash, watchdog, or persistent serial error | Pending |
| Display and buttons | Colors and circuit outlines are correct without clipping; UP/DOWN/OK short and long presses, back, and screen wake work | Pending |
| Offline content | Calendar, circuit details, sessions, and pending standings are correct; saved data remains available offline | Pending |
| Setup pages | Device QR and backup details are correct; phone page shows real nearby networks and allows manual entry | Pending |
| Setup safety | Wrong password does not replace saved credentials; correct password joins 2.4 GHz Wi-Fi and reconnects after reboot | Pending |
| Time and data | Clock syncs; current season, driver/team standings, and completed session podiums update; failures preserve old caches | Pending |
| Alerts and audio | Alert switch persists; button and alert sounds work; observe a real ten-minute pre-race alert separately | Pending |
| Power and battery | Dims at 30 seconds, sleeps at 90 seconds, wakes on a button; record battery display and charge LED | Pending |
| Continued operation | Repeated page navigation, setup, and network requests do not restart or noticeably slow the device | Pending |
| Recovery | Build compatibility passes; device entry and BLE discovery are pending, without unnecessary writes | Pending |
| Charging diagnosis | Collect `battery_diag` across a charge LED transition and record voltage and raw SOC | Pending |
| Publication evidence | Obtain a fresh screen capture and receipt without secrets; restore normal image and repeat smoke tests | Pending |

A real scheduled alert, long-term idle power, and a complete charge/discharge curve cannot be proved by one evening's test. Mark them as follow-up observations if they are not covered.
