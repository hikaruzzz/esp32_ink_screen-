/*
 * 虚拟传感器：确定性输出，支持注入连续失败，用于验证错误计数与恢复路径。
 * 纯 C 实现，可直接在主机上编译测试。
 */
#include "sensor.h"

#include <stddef.h>

static uint32_t s_seq = 0;
static unsigned s_fail_remaining = 0;

/* 测试钩子：接下来 count 次读取依次返回 TIMEOUT / CRC，交替出现。 */
void sensor_mock_inject_failures(unsigned count)
{
    s_fail_remaining = count;
}

sensor_status_t sensor_init(void)
{
    s_seq = 0;
    s_fail_remaining = 0;
    return SENSOR_OK;
}

sensor_status_t sensor_read(sensor_sample_t *out)
{
    if (out == NULL) {
        return SENSOR_ERR_INVALID_ARG;
    }
    if (s_fail_remaining > 0) {
        const sensor_status_t st = (s_fail_remaining % 2U) ? SENSOR_ERR_TIMEOUT : SENSOR_ERR_CRC;
        s_fail_remaining--;
        return st;
    }

    s_seq++;
    /* 缓慢变化的确定性数据，便于观察与断言 */
    out->temperature_milli_c = 25000 + (int32_t)(s_seq % 100U) * 100;
    out->humidity_milli_pct = 50000 + (int32_t)(s_seq % 50U) * 100;
    out->seq = s_seq;
    return SENSOR_OK;
}
