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
 *   ・2026-11-11 止まった右でこのキーを押したら戻った (FIX H>H 0 -5)。同じ読み取りを自動で行う仕組みを
 *     src/pointing/lpps_rescue.c に作り、読み取りの中身はそちら (split54tp_lpps_reread) に移した。このキーは確かめる用に残す。
 *   https://github.com/sekigon-gonnoc/zmk-driver-lpps
 */

#define DT_DRV_COMPAT zmk_behavior_tp_fix

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <split54tp/lpps_rescue.h>
#include <split54tp/tp_fix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(split54tp_tp_fix_done);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_COMPAT_STATUS_OKAY(lpps)

/* system work queue で読む (ドライバの motion_work と同じ queue) */
static void tp_fix_work_cb(struct k_work *work) {
    struct split54tp_lpps_reread_result rr;
    split54tp_lpps_reread(&rr, true);

    LOG_INF("TP fix: motion %d -> %d, read %d, close %d", rr.motion_before, rr.motion_after,
            rr.read_ret, rr.close_ret);

    raise_split54tp_tp_fix_done((struct split54tp_tp_fix_done){
        .motion_before = rr.motion_before,
        .motion_after = rr.motion_after,
        .read_ret = rr.read_ret,
        .close_ret = rr.close_ret,
    });
}

K_WORK_DEFINE(tp_fix_work, tp_fix_work_cb);

static int on_tp_fix_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
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
