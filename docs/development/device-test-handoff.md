<p align="right">
  <a href="device-test-handoff.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Device testing on another computer

Use the verified normal `FoloToy-AI-Passport-full.bin` and record its SHA-256 and
Git commit. There are no charging-diagnostic or USB screenshot firmware variants.
Follow the [build and test guide](build-and-test.md) for installation, and the
[device release checklist](device-release-checklist.md) for physical acceptance.

For a fault report, close other serial monitors and capture warnings/errors:

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300
```

The collector is read-only and never flashes, erases or sends device commands.
Keep private raw logs outside the repository and redact personal or network
information before sharing. Photograph device screens without exposing setup
passwords. Native simulator previews remain available for layout review.

Build success and simulator checks do not establish hardware acceptance. Mark
untested buttons, display, data synchronization, audio and power checks pending.
