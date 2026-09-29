<p align="right">
  <strong>简体中文</strong> · <a href="device-test-handoff.md">English</a>
</p>

# PDKPASS 跨电脑真机验收

适用于在另一台 macOS 电脑上检查 `main` 的候选固件。此流程只准备和测试固件；真机结果通过后再发布社区项目和 GitHub Release。

## 1. 从 GitHub 取得代码和两份固件

在新电脑克隆 `https://github.com/LeopardDennis/ai-passport-pdkpass`，或更新已有仓库。先记录 `git rev-parse HEAD` 的完整提交号，并与 GitHub Actions 构建运行页显示的提交号核对。

打开仓库 **Actions → Build firmware → Run workflow**，选择 `main`，勾选 **Also build charging logs and USB screenshot capture for device testing**。这会生成两个独立的下载包：

- `passport-main`：内含 `FoloToy-AI-Passport-full.bin`，是功能验收和最终发布候选。
- `passport-main-hardware-diagnostics`：内含 `FoloToy-AI-Passport-hardware-diagnostics-full.bin`，每 60 秒记录电池原始读数，同时支持 USB 屏幕采集；仅用于排障和发布封面取证。

等待同一次运行的两个构建及上传步骤全部成功，再下载并解压。手动运行选择的是分支，不会创建 GitHub Release。记录两份文件的大小和 `shasum -a 256 文件名` 输出；不要把两份文件混用。`build/` 目录没有入 Git，因此只 `git clone` 不会得到 `.bin`。

## 2. 刷入并检查普通版

先使用普通版。仓库的 [`CI-build-and-release.md`](CI-build-and-release.md) 说明了浏览器刷机入口：选择完整合并镜像，从 `0x0` 写入。不要执行 `erase-flash`；设备身份 `cardid` 和永久 Recovery 必须保留。若刷机工具检测到的 Flash 不是 8 MB，先停下核对设备。

按 [`真机验收表`](device-release-checklist.zh_CN.md) 的项目检查启动、按键、画面、配网、联网、数据、提醒、声音、电源和稳定性。换电脑时可把每项结果直接发到聊天中。

新电脑克隆仓库后，可用标准库脚本采集 USB 串口日志：

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300 --output "$HOME/Desktop/pdkpass-$(date +%Y%m%d-%H%M%S).log"
```

日志文件发回聊天时，先检查并遮盖个人信息、网络名称或密码。记录出错前后的操作、屏幕内容、时间和日志。不要只发最后一行报错。

## 3. 排查充电与采集实机屏幕

普通版功能验收完成后，如需排查充电状态，再刷硬件诊断版。记录充电灯熄灭时间，采集其前后至少几条 `battery_diag`；诊断版会增加采样，不用于续航验收。普通版默认关闭 USB 截图协议，社区发布所需的实时屏幕采集须在诊断版运行时完成，并保留采集回执。不要在公开封面中展示热点密码或其他单机秘密。

完成排障和屏幕采集后，再刷回通过验收的普通版，核对 SHA-256，并做最后一次启动、配网、联网和按键冒烟检查。需要真实比赛时刻的提醒和完整充放电曲线，必须另作长期观察，不应把短时间测试写成通过。
