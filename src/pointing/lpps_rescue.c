/*
 * split54tp: LPPS の読み直しと、止まったときの自動の読み直し (2026-11-11)
 *
 *   ・背景 (keyboard-project/docs/rev7-修正計画.md 手順 161〜168):
 *     電源を入れたまま置くと、LPPS が MOTION (「読んで」の合図) を出さなくなり、TP が止まることがある (多いのは右)。
 *     ドライバ (zmk-driver-lpps 7a4a67c) は MOTION の LOW のレベル割り込みでだけ読むので、マイコンを再起動しても戻らない。
 *     TP 復旧のキーで I2C の読み取りを 1 回送ったら戻った (2026-11-11、表示 FIX H>H 0 -5)。
 *     LPPS 本体のソースは非公開で、合図を出さなくなる理由は不明。
 *   ・split54tp_lpps_reread(): ドライバの lpps_motion_work_handler と同じ読み取り (i2c_read_dt で 3 バイト → バスを閉じる 1 バイト)。
 *     読んだ値は捨てる (カーソルは動かさない)。system work queue から呼ぶ (ドライバの motion_work と同じ queue なので同時には読まない)。
 *   ・自動の読み直し: 自分の側の LPPS から CONFIG_SPLIT54TP_LPPS_KEEPALIVE_MS (既定 2000 = 2 秒、ユーザー指定) 以上
 *     動きが来なければ、その間隔ごとに読み直す。スティックを使っている間は読まない。0 にすると止める。
 *     動きは Zephyr の入力の通知 (INPUT_CALLBACK_DEFINE) で LPPS の装置からだけ受ける。値が 0 の通知は数えない
 *     (読み直しのあとに LPPS が 0 を送ってきても、「使っている」とみなさないため)。
 *     起動して 2 秒後にも 1 回読む (眠りから起きたときや、書き込み直後に LPPS が止まっていた場合のため)。
 *   ⚠ 未確認: 読み直しのたびに LPPS が合図を出してドライバが 0 の動きを報告すると、ZMK はそれを「操作」とみなすので、
 *     OLED が消えない・深い眠りに入らないことがありうる (ZMK の activity.c は入力の通知をすべて操作として数える)。実機で確かめる。
 *   https://github.com/sekigon-gonnoc/zmk-driver-lpps
 *   https://docs.zephyrproject.org/latest/services/input/index.html
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <split54tp/lpps_rescue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LPPS_NODE DT_INST(0, lpps)

static const struct i2c_dt_spec lpps_i2c = I2C_DT_SPEC_GET(LPPS_NODE);
static const struct gpio_dt_spec lpps_motion = GPIO_DT_SPEC_GET(LPPS_NODE, irq_gpios);

/* 読み取りのあと、MOTION が変わるのを待つ時間 (表示用) */
#define REREAD_SETTLE_MS 5

static int8_t read_motion(void) {
    int v = gpio_pin_get_dt(&lpps_motion); /* ACTIVE_LOW を付けていないので生の値 (0 = Low) */
    return (v < 0) ? -1 : (int8_t)v;
}

void split54tp_lpps_reread(struct split54tp_lpps_reread_result *r, bool check_after) {
    uint8_t buf[3] = {0};

    r->motion_after = -1;
    if (!i2c_is_ready_dt(&lpps_i2c) || !gpio_is_ready_dt(&lpps_motion)) {
        r->motion_before = -1;
        r->read_ret = -ENODEV;
        r->close_ret = -ENODEV;
        return;
    }

    r->motion_before = read_motion();
    r->read_ret = (int16_t)i2c_read_dt(&lpps_i2c, buf, sizeof(buf));
    r->close_ret = (int16_t)i2c_read_dt(&lpps_i2c, buf, 1);
    if (check_after) {
        k_msleep(REREAD_SETTLE_MS);
        r->motion_after = read_motion();
    }
}

/* ---------------------------------------------------------------- 自動の読み直し */

#if CONFIG_SPLIT54TP_LPPS_KEEPALIVE_MS > 0

#define KEEPALIVE_MS CONFIG_SPLIT54TP_LPPS_KEEPALIVE_MS

/* LPPS から最後に (0 でない) 動きが来た時刻 (k_uptime_get_32)。0 = まだ来ていない */
static atomic_t last_motion_ms;

static void on_lpps_input(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);
    if (evt->value != 0) {
        atomic_set(&last_motion_ms, (atomic_val_t)k_uptime_get_32());
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(LPPS_NODE), on_lpps_input, NULL);

static void keepalive_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(keepalive_work, keepalive_work_cb);

static void keepalive_work_cb(struct k_work *work) {
    uint32_t quiet = k_uptime_get_32() - (uint32_t)atomic_get(&last_motion_ms);

    if (quiet < KEEPALIVE_MS) {
        /* 使っている。最後の動きから KEEPALIVE_MS たったら見直す */
        k_work_reschedule(&keepalive_work, K_MSEC(KEEPALIVE_MS - quiet));
        return;
    }

    struct split54tp_lpps_reread_result r;
    split54tp_lpps_reread(&r, false);
    LOG_DBG("LPPS keepalive: motion %d, read %d, close %d", r.motion_before, r.read_ret,
            r.close_ret);

    k_work_reschedule(&keepalive_work, K_MSEC(KEEPALIVE_MS));
}

static int keepalive_init(void) {
    k_work_schedule(&keepalive_work, K_MSEC(KEEPALIVE_MS));
    return 0;
}

SYS_INIT(keepalive_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_SPLIT54TP_LPPS_KEEPALIVE_MS > 0 */
