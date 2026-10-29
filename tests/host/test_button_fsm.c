#include <assert.h>
#include <stdio.h>

#include "app_button.h"

static const app_button_config_t DEFAULT_CFG = {
    .sample_ms = 10,
    .debounce_samples = 3,
    .min_press_ms = 20,
    .max_press_ms = 800,
};

static void test_valid_short_press(void)
{
    app_button_t btn;
    app_button_init(&btn, &DEFAULT_CFG, false, 0);

    assert(app_button_feed(&btn, true, 0) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 10) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 20) == APP_BUTTON_PRESSED);

    assert(app_button_feed(&btn, false, 120) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 130) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 140) == APP_BUTTON_SHORT_PRESS);
    assert(app_button_last_press_ms(&btn) == 120);
}

static void test_single_sample_glitch_is_ignored(void)
{
    app_button_t btn;
    app_button_init(&btn, &DEFAULT_CFG, false, 0);

    /* 稳定的按下状态 */
    assert(app_button_feed(&btn, true, 0) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 10) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 20) == APP_BUTTON_PRESSED);

    /* 单个采样毛刺不应产生任何事件 */
    assert(app_button_feed(&btn, false, 30) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 40) == APP_BUTTON_NONE);

    /* 真正释放：一次且仅一次短按 */
    assert(app_button_feed(&btn, false, 100) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 110) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 120) == APP_BUTTON_SHORT_PRESS);
    assert(app_button_last_press_ms(&btn) == 100);
}

static void test_ultra_short_contact_filtered_by_debounce(void)
{
    app_button_t btn;
    app_button_init(&btn, &DEFAULT_CFG, false, 0);

    /* 单次 10 ms 内完成的触点：永远达不到 3 个稳定采样 */
    assert(app_button_feed(&btn, true, 0) == APP_BUTTON_NONE);
    for (uint64_t t = 10; t <= 100; t += 10) {
        assert(app_button_feed(&btn, false, t) == APP_BUTTON_NONE);
    }
    assert(app_button_last_press_ms(&btn) == 0);
}

static void test_too_short_guard_path(void)
{
    /* debounce=1 时才能触达“过短”守卫（默认去抖会先过滤） */
    const app_button_config_t cfg = {
        .sample_ms = 1,
        .debounce_samples = 1,
        .min_press_ms = 20,
        .max_press_ms = 800,
    };
    app_button_t btn;
    app_button_init(&btn, &cfg, false, 0);

    assert(app_button_feed(&btn, true, 0) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 10) == APP_BUTTON_PRESSED);

    assert(app_button_feed(&btn, false, 15) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 16) == APP_BUTTON_IGNORED_TOO_SHORT);
    assert(app_button_last_press_ms(&btn) == 6);
}

static void test_long_press_ignored(void)
{
    app_button_t btn;
    app_button_init(&btn, &DEFAULT_CFG, false, 0);

    assert(app_button_feed(&btn, true, 0) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 10) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, true, 20) == APP_BUTTON_PRESSED);

    assert(app_button_feed(&btn, false, 2020) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 2030) == APP_BUTTON_NONE);
    assert(app_button_feed(&btn, false, 2040) == APP_BUTTON_IGNORED_TOO_LONG);
    assert(app_button_last_press_ms(&btn) == 2020);
}

int main(void)
{
    test_valid_short_press();
    test_single_sample_glitch_is_ignored();
    test_ultra_short_contact_filtered_by_debounce();
    test_too_short_guard_path();
    test_long_press_ignored();

    puts("button_fsm: all tests passed");
    return 0;
}
