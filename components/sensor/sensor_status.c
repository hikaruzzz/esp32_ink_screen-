#include "sensor.h"

const char *sensor_status_name(sensor_status_t status)
{
    switch (status) {
    case SENSOR_OK:
        return "OK";
    case SENSOR_ERR_INVALID_ARG:
        return "INVALID_ARG";
    case SENSOR_ERR_NOT_INIT:
        return "NOT_INIT";
    case SENSOR_ERR_NODEV:
        return "NO_DEVICE";
    case SENSOR_ERR_BUS:
        return "BUS_ERROR";
    case SENSOR_ERR_TIMEOUT:
        return "TIMEOUT";
    case SENSOR_ERR_CRC:
        return "CRC_ERROR";
    case SENSOR_ERR_UNSUPPORTED:
        return "UNSUPPORTED";
    default:
        return "UNKNOWN";
    }
}
