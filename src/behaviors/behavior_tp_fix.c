/*
 * split54tp: TP 復旧のキー (試し、2026-11-11)
 *
 *   ・背景: 電源を入れたまま置いておくと TP が止まることがある (多いのは右)。
 *     マイコンだけを再起動しても戻らず、LPPS の電源を切ると戻った (keyboard-project/docs/rev7-修正計画.md 手順 161〜163)。
 *     LPPS 本体のソースは公開されておらず、止まる理由も、電源を切らずに戻す方法も分からない。
 *   ・このキーは、押した側の LPPS に、ドライバ (zmk-driver-lpps 7a4a67c の src/lpps.c の lpps_motion_work_handler) が
 *     MOTION のたびにするのと同じ読み取りを送る: i2c_read_dt で 3 バイト → バスを閉じるために 1 バイト。
 *     止まったときに押して、TP が戻るか・LPPS が I2C に答えるかを見るためのもの。
 *   ・結果 (前後の MOTION の線の状態と、2 回の読み取りの結果) はイベント split54tp_tp_fix_done で知らせ、
 *     押した側の OLED の TP の行に 10 秒出す (src/display/status_screen.c)。ログにも出す。
 *   ・読み取りは system work queue で行う。ドライバの motion_work も同じ queue (k_work_submit) なので、
 *     ドライバの読み取りと同時には走らない。左では behavior が BT の受信のスレッドで呼ばれるので、そこでは I2C を使わない。
 *   ・locality は BEHAVIOR_LOCALITY_EVENT_SOURCE (押した側で動く。&tpcalib と同じ)。
 *     ⚠ ペリフェラルで動かす behavior のノード名は 8 文字以内 (ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN = 9)。キーマップでは "tpfix"。
 *   https://github.com/sekigon-gonnoc/zmk-driver-lpps
 */

#define DT_DRV_COMPAT zmk_behavior_tp_fix

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <split54tp/tp_fix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(split54tp_tp_fix_done);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_COMPAT_STATUS_OKAY(lpps)

#define LPPS_NODE DT_INST(0, lpps)

static const struct i2c_dt_spec lpps_i2c = I2C_DT_SPEC_GET(LPPS_NODE);
static const struct gpio_dt_spec lpps_motion = GPIO_DT_SPEC_GET(LPPS_NODE, irq_gpios);

/* 読み取りのあと、MOTION が変わるのを待つ時間 */
#define TP_FIX_SETTLE_MS 5

static int8_t read_motion(void) {
    int v = gpio_pin_get_dt(&lpps_motion); /* ACTIVE_LOW を付けていないので生の値 (0 = Low) */
    return (v < 0) ? -1 : (int8_t)v;
}

static void tp_fix_work_cb(struct k_work *work) {
    struct split54tp_tp_fix_done r;
    uint8_t buf[3] = {0};

    r.motion_before = read_motion();
    r.read_ret = (int16_t)i2c_read_dt(&lpps_i2c, buf, sizeof(buf));
    r.close_ret = (int16_t)i2c_read_dt(&lpps_i2c, buf, 1);
    k_msleep(TP_FIX_SETTLE_MS);
    r.motion_after = read_motion();

    LOG_INF("TP fix: motion %d -> %d, read %d (%02X %02X %02X), close %d", r.motion_before,
            r.motion_after, r.read_ret, buf[0], buf[1], buf[2], r.close_ret);

    raise_split54tp_tp_fix_done(r);
}

K_WORK_DEFINE(tp_fix_work, tp_fix_work_cb);

static int on_tp_fix_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    if (!i2c_is_ready_dt(&lpps_i2c) || !gpio_is_ready_dt(&lpps_motion)) {
        LOG_ERR("TP fix: I2C or GPIO not ready");
        return ZMK_BEHAVIOR_OPAQUE;
    }
    k_work_submit(&tp_fix_work);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_tp_fix_released(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_tp_fix_driver_api = {
    .binding_pressed = on_tp_fix_pressed,
    .binding_released = on_tp_fix_released,
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_fix_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_COMPAT_STATUS_OKAY(lpps) */
