/*
 * 真实 I2C 传感器占位实现。
 *
 * 传感器型号与 I2C 引脚尚未确认（见 docs/pinmap.md），因此这里不猜测引脚、
 * 也不编造驱动：在引脚确认前返回 UNSUPPORTED，让上层按“传感器缺失”处理。
 * 型号确定后在此实现总线初始化、探测（i2c_master_probe）、读取与校验。
 */
#include "sensor.h"

#include "board.h"
#include "esp_log.h"

static const char *TAG = "sensor_i2c";

sensor_status_t sensor_init(void)
{
    if (BOARD_I2C_SDA_GPIO < 0 || BOARD_I2C_SCL_GPIO < 0) {
        ESP_LOGW(TAG, "I2C pins not confirmed (docs/pinmap.md); sensor disabled");
        return SENSOR_ERR_UNSUPPORTED;
    }

    ESP_LOGW(TAG, "I2C driver not implemented yet (sensor model/pins pending)");
    return SENSOR_ERR_UNSUPPORTED;
}

sensor_status_t sensor_read(sensor_sample_t *out)
{
    (void)out;
    return SENSOR_ERR_NOT_INIT;
}
