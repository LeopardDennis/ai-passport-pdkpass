<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 音乐/音效资源（Music / Sound Effects）

本目录存放项目可复用的音乐与音效资源。

## 如何使用

- 音频文件（如 `.wav`、`.mp3`、固件可用的编码格式）复制到本目录，并在本项目 `README.md` 记录采样率、时长、用途与来源。
- 与固件集成时，参考 [`components/bsp/include/bsp_audio.h`](../../components/bsp/include/bsp_audio.h) 音频接口，确认播放路径、任务上下文与内存占用。
- 音频文件占用 Flash 与内存，集成前请评估 ESP32-C3 无 PSRAM 的限制。

## 目录说明

> 当前为空骨架，用于存放后续加入的音乐/音效资源。加入资源时请同步更新本 `README.md` 的索引。

## 场次提醒

`session_reminder_pcm.inc` 是已确认的三秒铃声：两组三音上行，16 kHz、
有符号 16 位单声道。编译到只读 Flash，由声音任务分小块播放，不分配完整
PCM 内存缓冲区。codec 提醒音量为 80%，独立于 50% 的按键音量。

来源：本项目原创数学合成，2026-09-25 确认，遵循仓库许可证。频率为
659.255、783.991、1046.502 Hz，重复两次，带渐入、渐出和衰减。
采样取自已确认的试听文件，并移除了其 0.70 试听增益。
