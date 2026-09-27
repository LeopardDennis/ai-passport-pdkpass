<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Music and Sound Effects

Store reusable music and sound-effect sources here.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Session reminder

`session_reminder_pcm.inc` contains the approved three-second, two ascending
three-note chimes at 16 kHz, signed 16-bit mono. It is compiled into read-only
flash and streamed by the sound worker in short blocks; no full PCM RAM buffer
is allocated. The codec volume is 80%, separate from the 50% button volume.

Source: original mathematical synthesis made for this project and approved on
2026-09-25; covered by the repository license. Frequencies are 659.255, 783.991
and 1046.502 Hz, repeated twice, with faded attack/release and decay. Samples
were taken from the approved preview with its 0.70 preview gain removed.
