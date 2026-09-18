<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# PDKPASS macOS 模拟器

这个原生 Mac 模拟器直接运行正式固件中的 PDKPASS LVGL 页面、导航状态机、赛道轮廓、
分站主题、赛历、积分榜和成绩布局。它把设备真实的 240 × 320 RGB565 画面按最近邻方式
放大，不是另外复刻的一套网页效果图。

模拟器默认使用离线的 2026 赛历和积分榜快照。使用 `--year 2027 --race 1`
可预览内置的 2027 全部 24 站，具体时间待公布，不预设积分。成绩缓存按年份分开。
该参数仅供预览和测试，固件依照有效时间自动选择赛季。
在线状态下打开历史成绩页时，会按分站和
场次从 OpenF1 获取真实排名，并把已经完成的前三名保存在 macOS 用户缓存目录中。
数字键可以查看固件的各种联网状态，只有在线状态会同步成绩。

## 环境要求

- macOS
- Xcode Command Line Tools
- CMake 3.16 或更高版本
- 仓库中已生成的 `managed_components/` 目录
- 首次读取尚未缓存的历史成绩时需要联网

如果缺少 `managed_components/`，先正常执行一次 ESP-IDF 配置或构建，让锁定版本的
LVGL 依赖下载到仓库。

## 构建与运行

在仓库根目录执行：

```bash
./tools/pdkpass-simulator/run.sh
```

如果复制仓库时没有保留可执行权限，改用 `bash tools/pdkpass-simulator/run.sh`。等价的
手动命令是：

```bash
cmake -S tools/pdkpass-simulator -B build/pdkpass-simulator
cmake --build build/pdkpass-simulator -j
./build/pdkpass-simulator/pdkpass-simulator
```

默认首页预览第 13 站，也可以指定首页自动选中的分站，例如：

```bash
./tools/pdkpass-simulator/run.sh --race 1
```

## 操作方式

电量使用模拟值（默认 88），不是 Mac 或设备的实际电量。使用 `--battery 15`
检查低电量、`--battery 0` 检查零电量、`--battery -1` 检查未知电量。
正式界面的电池图标内部显示数字，不带百分号或 BAT 文字；未知电量只显示轮廓。
网络文字、日期和电池在状态栏彩色背景内部上下居中，不计算黑色边框或阴影。
真机仍由现有后台任务提供实际电量，界面不直接轮询传感器。

构建后运行 `python3 tools/pdkpass-simulator/test_status.py`（需要 Pillow），
检查像素居中、电量状态以及非法预览参数。

| Mac 输入 | 对应设备操作 |
| --- | --- |
| 上 / 下方向键 | 浏览 |
| 回车或空格 | 确认 |
| 按住回车 0.65 秒 | 返回 |
| Esc | 返回 |
| `1` / `2` / `3` / `4` / `5` | 配网 / 连接中 / 校时 / 在线 / 离线 |
| `S` 或 Command-S | 把当前 240 × 320 画面保存为 PNG |

如需不打开窗口直接渲染并做冒烟测试：

使用 `--page home|calendar|standings|track|results` 与 `--screenshot` 可渲染指定
实际页面；网络预览和成绩同步参数优先。

加入 `--network-view menu|retry|setup|confirm` 可截取实际网络界面。无线操作为
模拟行为，扫描与超时关闭仍需真机验证；配网截图使用示例凭据，不含设备密码。

```bash
./build/pdkpass-simulator/pdkpass-simulator \
  --race 13 --screenshot /tmp/pdkpass-simulator.png
```

如需不打开窗口，直接验证并截取真实的历史 FP1 成绩：

```bash
./build/pdkpass-simulator/pdkpass-simulator --race 1 --sync-results \
  --screenshot /tmp/pdkpass-australia-fp1.png
```

模拟器从 `sdkconfig.defaults` 读取 LVGL 内存池大小。构建后运行
`ctest --test-dir build/pdkpass-simulator --output-on-failure`，验证启动、配网、
积分榜、赛历、成绩、电量状态和唤醒过程不超出预算。此测试不测量设备系统堆或 Wi-Fi/TLS 内存。
