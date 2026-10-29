#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "app_mode.h"

int main(void)
{
    /* 模式循环 */
    assert(app_mode_next(APP_MODE_SLOW) == APP_MODE_FAST);
    assert(app_mode_next(APP_MODE_FAST) == APP_MODE_SOLID);
    assert(app_mode_next(APP_MODE_SOLID) == APP_MODE_SLOW);

    /* 非法输入回落到 SLOW */
    assert(app_mode_next((app_mode_t)-1) == APP_MODE_SLOW);
    assert(app_mode_next((app_mode_t)APP_MODE_COUNT) == APP_MODE_SLOW);

    /* 周期 */
    assert(app_mode_period_ms(APP_MODE_SLOW) == 1000);
    assert(app_mode_period_ms(APP_MODE_FAST) == 200);
    assert(app_mode_period_ms(APP_MODE_SOLID) == 0);
    assert(app_mode_period_ms((app_mode_t)-1) == 1000);

    /* 名称 */
    assert(strcmp(app_mode_name(APP_MODE_SLOW), "SLOW") == 0);
    assert(strcmp(app_mode_name(APP_MODE_FAST), "FAST") == 0);
    assert(strcmp(app_mode_name(APP_MODE_SOLID), "SOLID") == 0);

    puts("app_mode: all tests passed");
    return 0;
}
