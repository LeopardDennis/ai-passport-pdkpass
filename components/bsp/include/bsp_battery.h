// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// 校验并恢复官方原装电芯 profile；更换电芯时须使用匹配的供应商参数。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。检查更新标志和全部 80 字节参数，
// 缺失或不匹配时在休眠态写入并读回校验，随后启动电量计并最多等待 5 秒取得有效 SOC。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 剩余电量百分比 0..100;读失败返回 -1。读取 SOC 不额外读取电压。
// 初始化失败时最多每分钟重试一次；初始化成功后，异常 SOC 不触发周期复位。
// 初始化/读取须由调用方串行化，在工作任务中调用，不持有 LVGL 锁。
int bsp_battery_soc(void);

// 电池电压 mV;读失败返回 -1。
int bsp_battery_mv(void);
