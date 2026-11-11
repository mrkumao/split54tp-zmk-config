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
 *
 * ★ 2026-11-11 (5 回目) 「ドリフトしているときだけ校正する」形に変えた。
 *   ・背景: 校正はドライバが「次にスティックから値が来たとき」に行う (LPPS は値があるときだけ I2C に答える)。
 *     ドリフトしていないときに押すと、次にスティックに力がかかった瞬間 (触った、隣の G を押した など) の力を
 *     「0」として覚え、離すとその分がドリフトになる。実機: 左は V0 だったのに、A (校正) → G で急にドリフトし、
 *     電源を入れ直しても V8 (keyboard-project/docs/rev7-修正計画.md 手順 185・186)。
 *   ・やり方:
 *       1. 押したら CALIB_SETTLE_MS 待つ (指を離し、押した振動が収まるまで。この間に来た値は数えない)。
 *       2. そのあと CALIB_WINDOW_MS の間に、0 でない値が CALIB_MIN_VALUES 個来たら (= ドリフトしている)、
 *          ドライバに校正を頼む (lpps_request_write)。ドライバは次に値を読む直前に校正する。
 *       3. 来なければ何もしない (ドリフトしていないので校正は要らない)。
 *     結果はイベント split54tp_tp_calib_done で知らせ、押した側の OLED の TP の行に 10 秒出す
 *     ("CAL ok" / "CAL skip" / "CAL wait!" と、2 行目に値の最大 V と数 n)。
 *   ・"CAL wait!" = 頼んだのに CALIB_CONFIRM_MS の間に次の値が来なかった。ドライバの予約は消せないので、
 *     次にスティックに力がかかったときに校正されてしまう。そのときは電源を入れ直す (マイコンの再起動で予約は消える)。
 *   ・押した側の LPPS の入力の知らせは INPUT_CALLBACK_DEFINE で受ける (lpps_rescue.c とは別の受け口)。
 *     左では behavior が BT の受信のスレッドで呼ばれるが、ここでは時刻と印を付けるだけで I2C は使わない。
 */

#define DT_DRV_COMPAT zmk_behavior_tp_calib_local

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <split54tp/tp_calib.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(split54tp_tp_calib_done);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) && DT_HAS_COMPAT_STATUS_OKAY(lpps)

#define CALIB_SETTLE_MS 1500  /* 押してから数え始めるまで */
#define CALIB_WINDOW_MS 5000  /* ドリフトの値を待つ時間 */
#define CALIB_MIN_VALUES 3    /* この数の 0 でない値が来たら「ドリフトしている」とみなす */
#define CALIB_CONFIRM_MS 2000 /* 頼んでから、ドライバが次の値を読むまで待つ時間 */

enum { ST_IDLE = 0, ST_WATCH = 1, ST_REQUESTED = 2 };

/* zmk-driver-lpps の src/lpps.c にある公開関数 */
extern int lpps_request_write(const struct device *dev);

#define LPPS_DEV DEVICE_DT_GET(DT_INST(0, lpps))

static atomic_t state;      /* ST_* */
static atomic_t watch_from; /* この時刻 (k_uptime_get_32) から数える */
static atomic_t seen;       /* 数え始めてから来た 0 でない値の数 */
static atomic_t peak;       /* 同じ間の値の最大 (絶対値) */
static atomic_t result;     /* SPLIT54TP_CALIB_* */
static bool need_sync;      /* 頼んだときの読み取りの残り (同じ読み取りの Y) を飛ばす。入力のスレッドだけが触る */

static void calib_report_cb(struct k_work *work) {
    raise_split54tp_tp_calib_done((struct split54tp_tp_calib_done){
        .result = (uint8_t)atomic_get(&result),
        .peak = (uint16_t)MIN((uint32_t)atomic_get(&peak), 999U),
        .count = (uint16_t)MIN((uint32_t)atomic_get(&seen), 999U),
    });
}

K_WORK_DEFINE(calib_report_work, calib_report_cb);

static void calib_finish(int r) {
    atomic_set(&result, r);
    k_work_submit(&calib_report_work);
}

/* 時間切れ: 待つ間にドリフトが来なかった → 何もしない / 頼んだのにまだ行われていない → 知らせる */
static void calib_timeout_cb(struct k_work *work) {
    if (atomic_cas(&state, ST_WATCH, ST_IDLE)) {
        LOG_INF("TP calib: no drift (values %d, peak %d), skipped", (int)atomic_get(&seen),
                (int)atomic_get(&peak));
        calib_finish(SPLIT54TP_CALIB_SKIP);
    } else if (atomic_cas(&state, ST_REQUESTED, ST_IDLE)) {
        LOG_WRN("TP calib: requested, but the driver has not read a value yet");
        calib_finish(SPLIT54TP_CALIB_PENDING);
    }
}

K_WORK_DELAYABLE_DEFINE(calib_timeout_work, calib_timeout_cb);

static void on_calib_input(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    int st = (int)atomic_get(&state);

    if (st == ST_REQUESTED) {
        /* ドライバは校正を書いてから値を読む。頼んだあとの読み取りの値が来た = 校正が行われた */
        if (need_sync) {
            if (evt->sync) {
                need_sync = false;
            }
            return;
        }
        if (atomic_cas(&state, ST_REQUESTED, ST_IDLE)) {
            k_work_cancel_delayable(&calib_timeout_work);
            LOG_INF("TP calib: done (values %d, peak %d)", (int)atomic_get(&seen),
                    (int)atomic_get(&peak));
            calib_finish(SPLIT54TP_CALIB_DONE);
        }
        return;
    }

    if (st != ST_WATCH || evt->value == 0) {
        return;
    }
    if ((int32_t)(k_uptime_get_32() - (uint32_t)atomic_get(&watch_from)) < 0) {
        return; /* 押した直後 (指・振動が収まるのを待っている) */
    }

    atomic_val_t a = (atomic_val_t)abs(evt->value);
    if (a > atomic_get(&peak)) {
        atomic_set(&peak, a);
    }

    if (atomic_inc(&seen) + 1 < CALIB_MIN_VALUES) {
        return;
    }
    if (!atomic_cas(&state, ST_WATCH, ST_REQUESTED)) {
        return;
    }

    need_sync = !evt->sync;
    int ret = lpps_request_write(LPPS_DEV);
    if (ret < 0) {
        LOG_ERR("TP calib: request failed %d", ret);
        atomic_set(&state, ST_IDLE);
        k_work_cancel_delayable(&calib_timeout_work);
        calib_finish(SPLIT54TP_CALIB_ERROR);
        return;
    }
    LOG_INF("TP calib: drift seen (values %d, peak %d), requested", (int)atomic_get(&seen),
            (int)atomic_get(&peak));
    k_work_reschedule(&calib_timeout_work, K_MSEC(CALIB_CONFIRM_MS));
}

INPUT_CALLBACK_DEFINE(LPPS_DEV, on_calib_input, NULL);

static int on_tp_calib_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    if (!device_is_ready(LPPS_DEV)) {
        LOG_ERR("LPPS device not ready");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (atomic_get(&state) == ST_REQUESTED) {
        /* 前に頼んだ分を、ドライバがまだ行っていない (すぐ結果が出るので、ここでは何もしない) */
        return ZMK_BEHAVIOR_OPAQUE;
    }

    atomic_set(&seen, 0);
    atomic_set(&peak, 0);
    atomic_set(&watch_from, (atomic_val_t)(k_uptime_get_32() + CALIB_SETTLE_MS));
    atomic_set(&state, ST_WATCH);
    k_work_reschedule(&calib_timeout_work, K_MSEC(CALIB_SETTLE_MS + CALIB_WINDOW_MS));
    LOG_INF("TP calib: watching for drift");

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
