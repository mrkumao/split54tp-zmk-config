/*
 * split54tp: OLED の表示の内容を右から受け取る behavior (左 = ペリフェラルだけでコンパイル。2026-11-09)
 *
 *   右 (src/display/status_screen.c) が zmk_split_central_invoke_behavior() でこの behavior を
 *   「押した」ことにして、param1 / param2 に表示の内容を詰めて送ってくる (include/split54tp/status_sync.h)。
 *   キーには割り当てない。中身はそのまま左の画面 (status_screen.c) へ渡すだけ。
 *   ⚠ ノード名 "oledsync" は 8 文字以内 (ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN = 9)。split54tp.dtsi
 *   書き方は behavior_tp_calib_local.c と同じ。
 */

#define DT_DRV_COMPAT zmk_behavior_oled_sync

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <split54tp/status_sync.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_oled_sync_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    split54tp_status_apply_remote(binding->param1, binding->param2);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_oled_sync_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_oled_sync_driver_api = {
    .binding_pressed = on_oled_sync_pressed,
    .binding_released = on_oled_sync_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_oled_sync_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
