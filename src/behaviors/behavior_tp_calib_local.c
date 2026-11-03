/*
 * split54tp: LPPS の強制キャリブレーションを「キーを押した側」で実行する behavior (2026-11-04)
 *
 *   ドライバ同梱の &ps_calib は locality を指定していない (= セントラルで実行) ため、
 *   左で押しても右のセンサしか校正されない。
 *   ここでは locality を BEHAVIOR_LOCALITY_EVENT_SOURCE にして、押した側のセンサを校正する
 *   (&bootloader と同じ仕組み)。
 *   書き込み自体はドライバの lpps_request_write() に任せる。ドライバは次のモーション割込みで
 *   レジスタ 0x04 に 1 を書く (src/lpps.c の lpps_motion_work_handler)。
 *   https://github.com/sekigon-gonnoc/zmk-driver-lpps
 *
 *   ⚠ ペリフェラルで実行させる behavior のノード名は 8 文字以内 (ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN = 9)。
 *     キーマップのノード名は "tpcalib" にしてある。
 */

#define DT_DRV_COMPAT zmk_behavior_tp_calib_local

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_COMPAT_STATUS_OKAY(lpps)

/* zmk-driver-lpps の src/lpps.c にある公開関数 */
extern int lpps_request_write(const struct device *dev);

static int on_tp_calib_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    const struct device *lpps = DEVICE_DT_GET(DT_INST(0, lpps));

    if (!device_is_ready(lpps)) {
        LOG_ERR("LPPS device not ready");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    int ret = lpps_request_write(lpps);
    if (ret < 0) {
        LOG_ERR("Failed to request LPPS calibration: %d", ret);
    } else {
        LOG_INF("LPPS calibration requested");
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_tp_calib_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_tp_calib_driver_api = {
    .binding_pressed = on_tp_calib_pressed,
    .binding_released = on_tp_calib_released,
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_calib_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_COMPAT_STATUS_OKAY(lpps) */
