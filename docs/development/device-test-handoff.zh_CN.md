<p align="right">
  <strong>简体中文</strong> · <a href="device-test-handoff.md">English</a>
</p>

# 在另一台电脑进行真机测试

使用已验证的普通固件 `FoloToy-AI-Passport-full.bin`，记录 SHA-256 和 Git 提交。
不再提供充电诊断或 USB 截图固件。安装方式见[构建与测试](build-and-test.zh_CN.md)，
实机验收见[发布检查表](device-release-checklist.zh_CN.md)。

报告故障时，关闭其他串口监视器，采集警告和错误：

```bash
python3 tools/device-test/serial_capture.py --list-ports
python3 tools/device-test/serial_capture.py --seconds 300
```

采集工具只读，不刷写、不擦除，也不发送设备命令。原始日志保存在仓库外，分享前
遮盖个人和网络信息。真机画面使用照片记录，避免暴露配网密码；布局检查仍可使用
原生模拟器预览。

构建成功和模拟器检查不代表真机验收。未测的按键、显示、同步、音频和电源项目保持待测。
