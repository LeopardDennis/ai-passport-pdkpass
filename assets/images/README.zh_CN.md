<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 图片资源（Images）

本目录存放项目可复用的图片资源，如 UI 图标、背景、RGB565 资源等。

## 如何使用

- 图片文件复制到本目录，并在本项目 `README.md` 记录分辨率、格式、用途与来源。
- 与固件集成时，参考 [`components/bsp/include/bsp_display.h`](../../components/bsp/include/bsp_display.h) 与相关示例分支的图片资源管线，转换为固件所需格式（如 RGB565 数组）。
- 图片资源占用 Flash 与内存，集成前请评估 ESP32-C3 无 PSRAM 的限制。

## 目录说明

### 赛道几何

`circuits/` 保存于 2026-09-18 获取的四份
[bacinger/f1-circuits](https://github.com/bacinger/f1-circuits) GeoJSON 来源，
MIT 许可原文保存在 [circuits/LICENSE.txt](circuits/LICENSE.txt)。
这些是坐标数据，不是下载的地图画面。

| 本地文件 | 上游来源 |
| --- | --- |
| [portimao.geojson](circuits/portimao.geojson) | [pt-2008.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/pt-2008.geojson) |
| [istanbul.geojson](circuits/istanbul.geojson) | [tr-2005.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/tr-2005.geojson) |
| [sakhir.geojson](circuits/sakhir.geojson) | [bh-2002.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/bh-2002.geojson) |
| [jeddah.geojson](circuits/jeddah.geojson) | [sa-2021.geojson](https://github.com/bacinger/f1-circuits/blob/master/circuits/sa-2021.geojson) |

`tools/check_circuit_assets.py` 按平均纬度余弦投影经度、翻转纬度为屏幕坐标，
等比放入 192 × 61 坐标区并保留三像素边距。波尔蒂芒、伊斯坦布尔和吉达
等距采样为 48 段。萨基尔逆时针旋转 90 度，每次删除距相邻线段最近的点，
简化为 48 段，保留发夹弯而不在长直道上浪费点数；校验来源坐标到轮廓的
误差不超过一个显示像素。`main/pdkpass_tracks.c` 中每条赛道仍是 49 对闭合
字节坐标。UI 直接绘制这些坐标，萨基尔使用两像素线宽以分开相邻路段，
其他赛道保留四像素线宽。固件无需读取 GeoJSON 或在线下载几何。
上游文件名中的数字描述赛道历史，不限制可用于哪个赛季。
