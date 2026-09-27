// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// 保留芯片已有电池 profile；准确 SOC 需要与实际电芯匹配的供应商参数。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 剩余电量百分比 0..100;读失败返回 -1。读取 SOC 不额外读取电压。
int bsp_battery_soc(void);

// 电池电压 mV;读失败返回 -1。
int bsp_battery_mv(void);

// 只读诊断快照；每项读取失败时为 -1，其他成功项仍保留。
// raw_soc 单位为 1/256%，raw_vcell 保留寄存器原值，cell_mv 为转换后的 mV。
// ESP_OK 表示全部读取成功，不表示电量有效或充电完成。不会重启芯片或写参数。
typedef struct {
    int raw_soc;
    int raw_vcell;
    int cell_mv;
    int config;
    int version;
} bsp_battery_diagnostics_t;
esp_err_t bsp_battery_read_diagnostics(bsp_battery_diagnostics_t *sample);
