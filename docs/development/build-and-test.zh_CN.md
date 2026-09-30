<p align="right">
  <strong>简体中文</strong> · <a href="build-and-test.md">English</a>
</p>

# 构建与验证（Build & Test）

使用 ESP-IDF 5.5.3。全新机器或缺少工具链时，先按
[环境引导](environment-setup.zh_CN.md)完成安装。

> 固件编译优先运行 `./tools/validate.sh --firmware`，烧录优先把验证通过的
> `build/FoloToy-AI-Passport-full.bin` 写入空白设备；对已有身份的设备，只有合并文件
> 在保护区 `cardid` 之前结束时才可从 `0x0` 直刷，其余情况优先用小程序或分段
> `idf.py flash`。`idf.py build` 和
> `idf.py flash` 只作为增量开发命令，不作为默认交付方式。

PDKPASS 数据缓存使用 `0x310000` 处的 64 KB `pdk_cache` NVS 分区。
原有 24 KB `nvs` 保留 Wi-Fi 配置和小型系统数据。新分区位于固定的 3 MB
应用范围之后、受保护的 `cardid` 之前，由运行时初始化，不打包缓存内容。
从尚无该分区的固件升级时必须同时安装匹配的分区表；仅更新应用、保留旧分区表
无法使用新缓存分区。缓存通过应用 ELF SHA-256 归属当前固件镜像；换镜像后首次启动，
或没有镜像标记时，仅清理独立分区及默认 NVS 中的 `pdk_season`、`pdk_results`，
以及默认 NVS 中的 `pdk_sync`。清理成功后才提交新镜像标记。同一镜像重启保留离线缓存，
不再导入默认 NVS 的旧缓存。Wi-Fi、时钟及提醒设置保持不变。清理失败时禁用缓存读写，
下次启动重试，不擦除任何完整分区。

```bash
source <ESP-IDF-v5.5.3-路径>/export.sh
idf.py --version             # 必须输出 ESP-IDF v5.5.3
./tools/validate.sh --firmware # 优先：编译并验证 0x0 合并固件
idf.py set-target esp32c3     # 配置目标芯片（fresh checkout 后/换 target 后运行）
idf.py build                  # 可选：增量 app 编译
idf.py flash monitor          # 可选：增量 app 烧录
idf.py fullclean              # 只清空过期生成状态（勿用于清理用户源码改动）
```

`idf.py fullclean` 不能让已有 `sdkconfig` 完整同步变更后的 defaults。需要重建
target 或已跟踪 defaults 时，先保留有意的本地设置，再运行
`idf.py set-target esp32c3`。

仓库提交 `dependencies.lock` 以固定 ESP-IDF Managed Components 的解析结果。修改 `idf_component.yml` 后必须使用 ESP-IDF 5.5.3 重新生成锁文件、review 版本变化并与 manifest 一起提交；普通构建不应产生未提交的锁文件差异。

固件门禁使用全新的临时构建目录，并从仓库 `sdkconfig.defaults` 生成隔离的 `sdkconfig`。它不会读取或覆盖开发者根目录的 `sdkconfig`，只把验证通过的合并镜像复制到 `build/FoloToy-AI-Passport-full.bin`。门禁同时强制检查[小程序 BLE 兼容契约](ble-recovery-compatibility.zh_CN.md)：保护分区地址、应用大小、分区表 MD5、保护区数据不入包，以及 Recovery bootloader hook。

当前基线含一个可独立运行的纯逻辑测试：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

统一验证入口：

```bash
./tools/validate.sh --static    # 仓库一致性、workflow、文档链接、敏感信息、host tests
./tools/validate.sh --firmware  # ESP-IDF build、merge-bin、偏移与 BLE 兼容校验
./tools/validate.sh             # 完整验证
```

完整验证要求预先激活 ESP-IDF 5.5.3。CI 与本地使用同一脚本；若 CI 和本地行为不同，应先修复脚本或环境，而不是维护两份命令。

涉及物理外设的改动必须在真机运行硬件指南验收清单，并把“编译通过”与“硬件验证通过”分开记录。

## macOS 真机日志采集

发布固件已移除充电采样和 USB 截图调试入口。串口仅保留必要的警告和错误；
下面的采集工具可记录启动或联网故障，不进行刷写或充电采样。

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300
# 如果无法唯一识别 /dev/cu.usbmodem*，从列表中指定实际端口：
python3 tools/device-test/serial_capture.py --port /dev/cu.usbmodemXXXX --seconds 300
```

先开始采集，再重启设备一次，才能记录完整启动与重新联网过程。采集工具不会烧录、
擦除或向设备发送命令。原始日志以私有权限保存在 `/tmp/pdkpass-device-logs/`，终端
只汇总观察到的警告和错误消息。这些计数不能证明屏幕、按键、声音、电池或
定时行为正常；这些项目仍按硬件指南在真机验收。分享原始日志前应检查并遮盖网络或
个人信息。

社区只能上传验证通过的 `build/FoloToy-AI-Passport-full.bin`，不得上传应用单镜像
`build/FoloToy-AI-Passport.bin`，后者没有小程序可安全解析与转换的完整结构。
