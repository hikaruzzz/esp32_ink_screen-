/*
 * 纯业务逻辑：LED 工作模式与应用状态，不依赖 ESP-IDF，可主机测试。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_MODE_SLOW = 0,  /* 慢闪：1000 ms 翻转 */
    APP_MODE_FAST,      /* 快闪：200 ms 翻转 */
    APP_MODE_SOLID,     /* 常亮 */
    APP_MODE_COUNT
} app_mode_t;

/* 返回下一个模式（循环）。非法输入回落到 APP_MODE_SLOW。 */
app_mode_t app_mode_next(app_mode_t mode);

/* 模式的稳定名称，用于日志。 */
const char *app_mode_name(app_mode_t mode);

/* 模式对应的 LED 翻转周期（ms）；0 表示常亮。 */
unsigned app_mode_period_ms(app_mode_t mode);

#ifdef __cplusplus
}
#endif
