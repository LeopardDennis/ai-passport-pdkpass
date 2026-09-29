<p align="right">
  <strong>简体中文</strong> · <a href="device-test-handoff.md">English</a>
</p>

# PDKPASS 跨电脑充电排查与真机验收

适用于在另一台 macOS 电脑上先排查充电问题，再检查 `main` 的候选固件。充电问题解决且真机验收通过后，才准备社区项目和 GitHub Release。

## 1. 从 GitHub 取得代码和充电诊断固件

在新电脑克隆 `https://github.com/LeopardDennis/ai-passport-pdkpass`，或更新已有仓库。先记录 `git rev-parse HEAD` 的完整提交号，并与 GitHub Actions 构建运行页显示的提交号核对。

打开仓库 **Actions → Build firmware → Run workflow**，选择 `main`，只勾选 **Also build read-only battery logs for charging diagnosis**。这会生成两个独立的下载包：

- `passport-main`：内含 `FoloToy-AI-Passport-full.bin`，留待充电问题解决后做功能验收。
- `passport-main-battery-diagnostics`：内含 `FoloToy-AI-Passport-battery-diagnostics-full.bin`，每 60 秒记录电池原始读数；先用它排查充电。

等待同一次运行的两个构建及上传步骤全部成功，再下载并解压。手动运行选择的是分支，不会创建 GitHub Release。记录两份文件的大小和 `shasum -a 256 文件名` 输出；不要把两份文件混用。`build/` 目录没有入 Git，因此只 `git clone` 不会得到 `.bin`。

## 2. 先排查充电

先刷充电诊断版。仓库的 [`CI-build-and-release.md`](CI-build-and-release.md) 说明了浏览器刷机入口：选择完整合并镜像，从 `0x0` 写入。不要执行 `erase-flash`；设备身份 `cardid` 和永久 Recovery 必须保留。若刷机工具检测到的 Flash 不是 8 MB，先停下核对设备。

在新电脑克隆仓库后，用标准库脚本采集 USB 串口日志：

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300 --output "$HOME/Desktop/pdkpass-$(date +%Y%m%d-%H%M%S).log"
```

保持设备连接电脑并充电，连续记录 `battery_diag`。如果能顺手观察到充电灯熄灭的时间，可告知 Codex 以对齐日志；不观察也可以先分析电压和原始电量的趋势。如果 300 秒不足以覆盖变化，可把 `--seconds` 调长。拔掉同一根 USB 线会中断串口记录；静置电压只能另行测量。诊断版会增加日志采样，不用于续航验收。

日志文件发回聊天时，先检查并遮盖个人信息、网络名称或密码；附上指示灯状态和对应时间。不要只发最后一行报错。单凭一次 `4196 mV / 96% / 灯灭` 不能判定电芯没充满，也不能确定是电量计、充电芯片还是指示灯行为导致。

## 3. 问题解决后再做全功能验收与发布准备

充电问题确认并解决后，刷回普通版，按 [`真机验收表`](device-release-checklist.zh_CN.md) 检查启动、按键、画面、配网、联网、数据、提醒、声音、电源和稳定性。需要采集社区发布用的实时屏幕时，另行手动运行工作流并勾选 **Also build charging logs and USB screenshot capture for device testing**，下载 `passport-main-hardware-diagnostics`。它支持 `FAP_SCREENSHOT_V1`；采集时保留回执，不要在公开封面中展示热点密码或其他单机秘密。

完成屏幕采集后，再刷回通过验收的普通版，核对 SHA-256，并做最后一次启动、配网、联网和按键冒烟检查。需要真实比赛时刻的提醒和完整充放电曲线，必须另作长期观察，不应把短时间测试写成通过。
