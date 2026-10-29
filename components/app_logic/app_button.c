#include "app_button.h"

#include <stddef.h>

void app_button_init(app_button_t *btn, const app_button_config_t *cfg,
                     bool initial_pressed, uint64_t now_ms)
{
    if (btn == NULL || cfg == NULL) {
        return;
    }
    btn->cfg = *cfg;
    if (btn->cfg.debounce_samples == 0) {
        btn->cfg.debounce_samples = 1;
    }
    btn->stable = initial_pressed;
    btn->candidate = initial_pressed;
    btn->candidate_cnt = 0;
    btn->press_start_ms = now_ms;
    btn->last_press_ms = 0;
}

app_button_event_t app_button_feed(app_button_t *btn, bool pressed, uint64_t now_ms)
{
    if (btn == NULL) {
        return APP_BUTTON_NONE;
    }

    if (pressed == btn->stable) {
        /* 与稳定状态一致：丢弃候选抖动 */
        btn->candidate = btn->stable;
        btn->candidate_cnt = 0;
        return APP_BUTTON_NONE;
    }

    if (pressed != btn->candidate) {
        /* 新候选边沿 */
        btn->candidate = pressed;
        btn->candidate_cnt = 1;
        return APP_BUTTON_NONE;
    }

    if (++btn->candidate_cnt < btn->cfg.debounce_samples) {
        return APP_BUTTON_NONE;
    }

    /* 候选稳定，确认状态变化 */
    btn->stable = pressed;
    btn->candidate_cnt = 0;

    if (pressed) {
        btn->press_start_ms = now_ms;
        return APP_BUTTON_PRESSED;
    }

    const uint32_t duration_ms = (uint32_t)(now_ms - btn->press_start_ms);
    btn->last_press_ms = duration_ms;

    if (duration_ms < btn->cfg.min_press_ms) {
        return APP_BUTTON_IGNORED_TOO_SHORT;
    }
    if (duration_ms > btn->cfg.max_press_ms) {
        return APP_BUTTON_IGNORED_TOO_LONG;
    }
    return APP_BUTTON_SHORT_PRESS;
}

uint32_t app_button_last_press_ms(const app_button_t *btn)
{
    return btn == NULL ? 0 : btn->last_press_ms;
}
