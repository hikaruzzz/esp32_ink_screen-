/*
 * 传感器抽象层（不依赖 ESP-IDF，便于主机测试）。
 *
 * 当前默认使用虚拟驱动（sensor_mock.c），用于在型号与 I2C 引脚确认前
 * 验证采样流程、错误计数与业务不阻塞；真实驱动实现见 sensor_i2c.c。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENSOR_OK = 0,
    SENSOR_ERR_INVALID_ARG,
    SENSOR_ERR_NOT_INIT,
    SENSOR_ERR_NODEV,
    SENSOR_ERR_BUS,
    SENSOR_ERR_TIMEOUT,
    SENSOR_ERR_CRC,
    SENSOR_ERR_UNSUPPORTED,
} sensor_status_t;

typedef struct {
    int32_t temperature_milli_c;  /* 温度，毫摄氏度（25000 = 25.000 °C） */
    int32_t humidity_milli_pct;   /* 相对湿度，千分之一百分比（50000 = 50.000 %） */
    uint32_t seq;                 /* 驱动侧采样序号，从 1 开始 */
} sensor_sample_t;

/* 初始化传感器子系统。失败不影响 LED/按键等本地功能。 */
sensor_status_t sensor_init(void);

/* 读取一次采样。失败时不修改 *out。 */
sensor_status_t sensor_read(sensor_sample_t *out);

/* 状态的稳定名称，用于日志。 */
const char *sensor_status_name(sensor_status_t status);

#ifdef __cplusplus
}
#endif
