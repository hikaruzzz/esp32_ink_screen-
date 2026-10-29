/*
 * 按键去抖与短按判定（纯逻辑，不依赖 ESP-IDF，可主机测试）。
 *
 * 规则：
 *   - 连续 debounce_samples 次采样稳定才确认状态变化（10 ms × 3 = 30 ms）
 *   - 按下时长在 [min_press_ms, max_press_ms] 内 → SHORT_PRESS
 *   - 短于 min_press_ms → IGNORED_TOO_SHORT（去抖守卫，正常配置下多被去抖窗口过滤）
 *   - 长于 max_press_ms → IGNORED_TOO_LONG
 *   - 每次物理按压至多产生一个 SHORT_PRESS
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_ms;        /* 采样周期，毫秒 */
    uint32_t debounce_samples; /* 连续稳定采样数，至少 1 */
    uint32_t min_press_ms;     /* 短按下限 */
    uint32_t max_press_ms;     /* 短按上限 */
} app_button_config_t;

typedef enum {
    APP_BUTTON_NONE = 0,
    APP_BUTTON_PRESSED,
    APP_BUTTON_SHORT_PRESS,
    APP_BUTTON_IGNORED_TOO_SHORT,
    APP_BUTTON_IGNORED_TOO_LONG,
} app_button_event_t;

typedef struct {
    app_button_config_t cfg;
    bool stable;
    bool candidate;
    uint32_t candidate_cnt;
    uint64_t press_start_ms;
    uint32_t last_press_ms;
} app_button_t;

void app_button_init(app_button_t *btn, const app_button_config_t *cfg,
                     bool initial_pressed, uint64_t now_ms);

/* 喂入一次采样（pressed=true 表示按键处于按下状态），返回产生的事件。 */
app_button_event_t app_button_feed(app_button_t *btn, bool pressed, uint64_t now_ms);

/* 最近一次完成的按压时长（毫秒），用于日志。 */
uint32_t app_button_last_press_ms(const app_button_t *btn);

#ifdef __cplusplus
}
#endif
