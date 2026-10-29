#include "app_mode.h"

app_mode_t app_mode_next(app_mode_t mode)
{
    if (mode < 0 || mode >= APP_MODE_COUNT) {
        return APP_MODE_SLOW;
    }
    return (app_mode_t)((mode + 1) % APP_MODE_COUNT);
}

const char *app_mode_name(app_mode_t mode)
{
    switch (mode) {
    case APP_MODE_SLOW:
        return "SLOW";
    case APP_MODE_FAST:
        return "FAST";
    case APP_MODE_SOLID:
        return "SOLID";
    default:
        return "UNKNOWN";
    }
}

unsigned app_mode_period_ms(app_mode_t mode)
{
    switch (mode) {
    case APP_MODE_SLOW:
        return 1000;
    case APP_MODE_FAST:
        return 200;
    case APP_MODE_SOLID:
        return 0;
    default:
        return 1000;
    }
}
