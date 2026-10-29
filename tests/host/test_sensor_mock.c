#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sensor.h"

/* 仅虚拟驱动提供的测试钩子（见 sensor_mock.c） */
void sensor_mock_inject_failures(unsigned count);

int main(void)
{
    sensor_sample_t sample;

    assert(sensor_init() == SENSOR_OK);

    /* 空指针拒绝 */
    assert(sensor_read(NULL) == SENSOR_ERR_INVALID_ARG);

    /* 正常采样：序号递增，数值确定 */
    assert(sensor_read(&sample) == SENSOR_OK);
    assert(sample.seq == 1);
    assert(sample.temperature_milli_c == 25100);
    assert(sample.humidity_milli_pct == 50100);

    /* 注入 2 次失败：先 CRC 再 TIMEOUT，之后恢复 */
    sensor_mock_inject_failures(2);
    assert(sensor_read(&sample) == SENSOR_ERR_CRC);
    assert(sensor_read(&sample) == SENSOR_ERR_TIMEOUT);
    assert(sensor_read(&sample) == SENSOR_OK);
    assert(sample.seq == 2);

    /* 状态名称 */
    assert(strcmp(sensor_status_name(SENSOR_ERR_TIMEOUT), "TIMEOUT") == 0);
    assert(strcmp(sensor_status_name(SENSOR_OK), "OK") == 0);

    puts("sensor_mock: all tests passed");
    return 0;
}
