// components/bsp/src/bsp_i2c.c
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"

static const char *TAG = "bsp_i2c";

static i2c_master_bus_handle_t s_bus;

esp_err_t bsp_i2c_init(void) {
    if (s_bus) return ESP_OK;                 // 幂等
    i2c_master_bus_config_t cfg = {
        .i2c_port = BSP_I2C_PORT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线创建失败 (%s) —— 检查 SDA=GPIO%d / SCL=GPIO%d 是否被别的外设占用",
                 esp_err_to_name(e), BSP_I2C_SDA, BSP_I2C_SCL);
        s_bus = NULL;
        return e;
    }
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_bus(void) { return s_bus; }
