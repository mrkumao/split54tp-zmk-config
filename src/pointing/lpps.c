/*
 * Copyright 2025 sekigon-gonnoc
 * SPDX-License-Identifier: GPL-2.0 or later
 */

/*
 * split54tp: LPPS のドライバ (zmk-driver-lpps を取り込んで手を入れたもの)
 *
 *   取り込み元: https://github.com/sekigon-gonnoc/zmk-driver-lpps の src/lpps.c (コミット 7a4a67c)
 *   ライセンス: このファイルの先頭の表示 (GPL-2.0 or later) のまま。リポジトリの LICENSE は MIT。
 *     どちらが優先かは不明なので、条件の厳しい GPL に合わせて、元の表示を残し、変えた所と日付を書く。
 *     詳しくは src/pointing/lpps.LICENSE.txt。
 *   上流のドライバは config/split54tp.conf の CONFIG_LPPS=n でビルドから外し、こちらを CONFIG_SPLIT54TP_LPPS で使う。
 *   devicetree の compatible "lpps" と、その binding (上流の dts/bindings/lpps-calibration.yaml) はそのまま使う。
 *
 * ★ 2026-11-14 変えた所 (split54tp, keyboard-project/docs/rev7-修正計画.md 手順 204・205)
 *   1. 差し引く形 (dead-zone) をここで行い、X も Y も 0 になった読み取りは入力の知らせを出さない。
 *      背景: ZMK は値が 0 の知らせも「操作」と数え (app/src/activity.c)、Mac へ動き 0 のマウスの報告も送る
 *      (app/src/pointing/input_listener.c)。読み直しや小さなドリフトのたびに知らせが出て、キーボードも Mac も眠らなかった。
 *      引く量は CONFIG_SPLIT54TP_LPPS_DEAD_ZONE (左 2 = 既定 / 右 6 = split54tp_right.conf)。
 *      右の input processor (tp_drift_l / tp_drift_r) の dead-zone は 0 にした (二重に引かないように)。
 *   2. 3 バイトの読み取りに失敗しても、バスを閉じる 1 バイトの読み取りを行う (元は飛ばしていた)。
 *      LPPS が合図 (MOTION) を出さなくなる原因の候補 (手順 166)。未確認。
 *   3. 読み取りのたびに生の値を split54tp の口に渡す (引く前の値。OLED の V / Z / M と、TP 校正のドリフトの判定に使う)。
 *      校正 (レジスタ 0x04 への書き込み) のあとにも結果を渡す (校正のキーの OLED の表示に使う)。
 *   4. 読み取りの前に、受け取る配列を 0 にした (失敗したときのログに古い値が出ないように)。
 *
 * ★ 2026-11-15 変えた所 (keyboard-project/docs/rev7-修正計画.md 手順 209)
 *   5. 差し引く形を「X / Y 別々」から「力の大きさ (X と Y を合わせた長さ)」に変えた。向きはそのまま。
 *      ユーザー: 「円を描くように操作したのに四角になる」。X / Y 別々に引くと、横に近い向きでは縦の力が 0 になり
 *      真横にだけ動く (縦も同じ) ので、円の上下左右が平らになっていた。
 *      長さ m = √(x² + y²) から CONFIG_SPLIT54TP_LPPS_DEAD_ZONE を引き、x / y を (m − 引く量) / m 倍する。
 *      割り算の端数は次の読み取りへ繰り越す (ゆっくり動かしたときに動きが消えないように。止まったら捨てる)。
 *   6. 止まっている状態から動き始めるときは、引く量を超えた読み取りが CONFIG_SPLIT54TP_LPPS_START_READS 回
 *      続いてから知らせる (1 回だけ跳ねた値を捨てる)。間が LPPS_START_GAP_MS より空いたら数え直す。
 *      実測 (手順 208・209): 触らずに 3 分で左は最大 31 / 8 の値がときどき出て、Mac へ動きを送り続け、画面が消えなかった。
 *      動かしている間の読み取りは 1 秒に約 140 回 (右、3 秒で 419 回) なので、3 回で約 0.02 秒の遅れ。
 *
 * ★ 2026-11-15 (2 回目) 変えた所 (手順 211)
 *   7. 止まっているときの 0 の位置 (基準) を、ゆっくり自動で合わせ直す。
 *      実測 (手順 210): 5・6 のあとも、触らずに 3 分で右は最大 10 の値が 6 を超えて 3 回以上続き、約 20 回 Mac へ動きを送った
 *      (右の OLED が点き、Mac の画面も消えなかった)。1 回だけの跳ねではなく、ゆっくりしたずれも混じる。
 *      止まっているときの読み取りは 1 秒に約 18 回 (右、10 秒で 176 回)、動かしているときは約 140 回。
 *      やり方: 基準からの力の大きさが LPPS_BASE_TRACK_MAX (引く量の 2 倍、右左とも 12) 以下のとき、
 *      基準を今の値へ寄せる (引く量以下 = 止まっているときは時定数 4 秒、それを超えて 12 以下は 60 秒でゆっくり)。
 *      引く量・動き始めの判定・知らせる値は、基準を引いた値で行う。
 *      ゆっくりしたずれは基準が追いかけるので、引く量を超えにくくなる。12 を超えて押したときは合わせ直さない。
 *      欠点: 力 7〜12 の弱い力で押し続けると、だんだん遅くなる (手元の計算で、力 9 を 3 秒で約 1 割)。
 *      止まっているときに急に 10 ほどずれると、基準が追いつくまでの 30 秒ほど、少しずつ動く。
 *      止まっているときの読み取りは、左も 1 秒に約 17 回 (10 秒で 165 回)。
 *      校正 (レジスタ 0x04) を書いたら、LPPS の 0 が変わるので基準を 0 に戻す。
 *
 * ★ 2026-11-15 (3 回目) 変えた所 (手順 214)
 *   8. 速さの曲線に「弱い力の幅の中は同じ速さ」の平らな所を入れた (CONFIG_SPLIT54TP_LPPS_PLATEAU、既定 6)。
 *      ユーザー: 「弱い力では動き始めはいい感じだが、途中で急に速くなることがある。スムーズとは言えない」。
 *      前は引いたあとの力にそのまま比例していたので、弱い力ほど指の力の揺れで速さが大きく変わった (力 8 → 10 で 2 → 4)。
 *      TrackPoint の考え方 (IBM の Rutledge・Selker): 弱い力の広い幅を、一定のごく遅い速さに対応させる ("dead slow" の平らな所)。
 *      ここでは、引く量を超えて 1 までは 0 → 1、そこから平らな幅 6 の間は 1 (1 回の読み取りで 1 = 1 秒に約 140)、
 *      その先は前と同じ傾きで速くなる (前より平らな幅の分だけ遅い。強く押したときの速さは感度で上げられる)。
 *
 * ★ 2026-11-18 変えた所 (手順 232)
 *   9. 読み取りのあと MOTION が CONFIG_SPLIT54TP_LPPS_STUCK_MS (既定 10) たっても Low のままなら、もう一度読む
 *      (lpps_irq_reenable_work_handler)。上流は High に戻るまで割り込みを入れずに待つだけなので、
 *      High の間を見逃すと、LPPS は読まれるのを、ドライバは High を待ち続けて止まる見込み (未確認)。
 *      ユーザー: 「突然 TP カーソルが止まる (特に右)。TP 復旧でだいたい戻る」。そのときの表示は「L>H ok」。
 */

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>

#include <split54tp/lpps_raw.h> /* ★ 2026-11-14 */

LOG_MODULE_REGISTER(lpps, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT lpps

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct lpps_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec irq_gpio;
    struct gpio_dt_spec power_gpio;
};

struct lpps_data {
    const struct device *dev;
    struct k_work motion_work;
    struct k_work_delayable irq_reenable_work;
    struct gpio_callback motion_cb;
    atomic_t behavior_calibration_pending;
    /* ★ 2026-11-15 差し引く形と動き始めの判定 (ドライバの読み取りのスレッドだけが触る) */
    int32_t rem_x;          /* 割り算の端数 (256 倍) */
    int32_t rem_y;
    uint8_t streak;         /* 引く量を超えた読み取りが続いた回数 */
    int64_t last_above_ms;  /* 最後に引く量を超えた時刻 */
    /* ★ 2026-11-15 (2 回目) 止まっているときの 0 の位置 (基準、65536 倍) と、前の読み取りの時刻 */
    int32_t base_x;
    int32_t base_y;
    int64_t last_read_ms;
    /* ★ 2026-11-18 読み取りのあと、MOTION が High に戻るのを待ち始めた時刻 (lpps_interrupt_enable_when_inactive) */
    int64_t wait_start_ms;
};

static int lpps_i2c_read_reg(const struct device *dev, uint8_t reg, uint8_t *data, uint8_t len) {
    const struct lpps_config *cfg = dev->config;
    // return i2c_burst_read_dt(&cfg->i2c, reg, data, len);
    // i2c_write_dt(&cfg->i2c, &reg, 1);
    return i2c_read_dt(&cfg->i2c, data, len);
}

static int lpps_i2c_write_reg(const struct device *dev, uint8_t reg, const uint8_t *data,
                              uint8_t len) {
    const struct lpps_config *cfg = dev->config;
    return i2c_burst_write_dt(&cfg->i2c, reg, data, len);
}

static int lpps_interrupt_configure(const struct device *dev, gpio_flags_t flags) {
    const struct lpps_config *cfg = dev->config;

    if (!gpio_is_ready_dt(&cfg->irq_gpio)) {
        return -ENODEV;
    }

    return gpio_pin_interrupt_configure_dt(&cfg->irq_gpio, flags);
}

static void lpps_interrupt_enable(const struct device *dev) {
    int ret = lpps_interrupt_configure(dev, GPIO_INT_LEVEL_LOW);

    if (ret < 0) {
        LOG_ERR("Failed to enable IRQ interrupt: %d", ret);
    }
}

static void lpps_interrupt_disable(const struct device *dev) {
    int ret = lpps_interrupt_configure(dev, GPIO_INT_DISABLE);

    if (ret < 0) {
        LOG_WRN("Failed to disable IRQ interrupt: %d", ret);
    }
}

static bool lpps_irq_is_active(const struct device *dev) {
    const struct lpps_config *cfg = dev->config;

    return gpio_pin_get_dt(&cfg->irq_gpio) == 0;
}

/*
 * ★ 2026-11-18 (手順 232) 読み取りのあと MOTION が High に戻るのを 1ms ごとに見て、戻ったら割り込みを入れる (上流と同じ)。
 *   ただし CONFIG_SPLIT54TP_LPPS_STUCK_MS たっても Low のままなら、もう一度読む (上流は High になるまで待つだけ)。
 *   止まった右で TP 復旧を押すと「L>H」(読む前は Low = LPPS が「読んで」と言っている、読んだら High) だった。
 *   見込み (未確認): High の間を 1ms ごとの見回りが見逃し、LPPS が次の値で Low にすると、
 *   ドライバは「High になるまで割り込みを入れない」、LPPS は「読まれるまで Low のまま」で、互いに待って止まる。
 *   Low は「読んで」の合図なので、もう一度読むのは正しい動き (TP 復旧のキーが手で行っていたことと同じ)。
 *   読んだ回数は OLED の TP 復旧の表示の R で分かる (src/pointing/lpps_rescue.c)。
 */
static void lpps_irq_reenable_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct lpps_data *data = CONTAINER_OF(dwork, struct lpps_data, irq_reenable_work);
    const struct device *dev = data->dev;

    if (lpps_irq_is_active(dev)) {
#if CONFIG_SPLIT54TP_LPPS_STUCK_MS > 0
        if (k_uptime_get() - data->wait_start_ms >= CONFIG_SPLIT54TP_LPPS_STUCK_MS) {
#if IS_ENABLED(CONFIG_SPLIT54TP_LPPS_RESCUE)
            split54tp_lpps_stuck_read();
#endif
            /* 割り込みは切ったまま。読み取りの最後で、また lpps_interrupt_enable_when_inactive に来る */
            k_work_submit(&data->motion_work);
            return;
        }
#endif
        k_work_reschedule(&data->irq_reenable_work, K_MSEC(1));
        return;
    }

    lpps_interrupt_enable(dev);
}

static void lpps_interrupt_enable_when_inactive(struct lpps_data *data) {
    const struct device *dev = data->dev;

    if (lpps_irq_is_active(dev)) {
        data->wait_start_ms = k_uptime_get(); /* ★ 2026-11-18 */
        k_work_reschedule(&data->irq_reenable_work, K_MSEC(1));
        return;
    }

    lpps_interrupt_enable(dev);
}

static bool lpps_is_ready(const struct device *dev) {
    const struct lpps_config *cfg = dev->config;

    if (!gpio_is_ready_dt(&cfg->irq_gpio)) {
        return true; // Assume ready if no IRQ pin configured
    }

    // RDY pin is active LOW, so device is ready when pin is LOW
    return !gpio_pin_get_dt(&cfg->irq_gpio);
}

/* ★ 2026-11-15 動き始めの判定: 引く量を超えた読み取りの間がこれより空いたら、数え直す */
#define LPPS_START_GAP_MS 200

/* ★ 2026-11-15 (2 回目) 基準の合わせ直し: 時定数、合わせ直す範囲 (基準からの力の大きさ)、1 回に数える時間の上限
 *   引く量以下 (止まっている) は LPPS_BASE_TAU_MS、引く量を超えて LPPS_BASE_TRACK_MAX 以下 (弱く押しているかもしれない) は
 *   LPPS_BASE_TAU_SLOW_MS でゆっくり。どちらも 4 秒にすると、力 9 で押し続けたとき 2 秒ほどで止まった (手元の計算)。
 *   30 秒でも力 9 で 3 秒押すと 24% 遅くなったので 60 秒にした。 */
#define LPPS_BASE_TAU_MS 4000
#define LPPS_BASE_TAU_SLOW_MS 60000
#define LPPS_BASE_TRACK_MAX MAX(2 * CONFIG_SPLIT54TP_LPPS_DEAD_ZONE, 4)
#define LPPS_BASE_DT_MAX_MS 250

/* ★ 2026-11-15 整数の平方根 (切り捨て)。(2 回目) 256 倍の値の 2 乗の和を入れるので 64 ビットにした */
static uint64_t lpps_isqrt(uint64_t v) {
    uint64_t r = 0;
    uint64_t bit = 1ULL << 62;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/*
 * ★ 2026-11-15 差し引く (力の大きさで) と、動き始めの判定。知らせる値があれば true を返し、*ox / *oy に入れる。
 *   長さ m が引く量以下 → 0 (止まっている。端数と数を捨てる)。
 *   止まっている状態からは、引く量を超えた読み取りが CONFIG_SPLIT54TP_LPPS_START_READS 回続くまで知らせない。
 *   知らせる値 = (x, y) × (m − 引く量) / m。端数 (256 倍) は次へ繰り越す。
 */
static bool lpps_filter(struct lpps_data *data, int8_t x, int8_t y, int32_t *ox, int32_t *oy) {
    const int32_t dz = CONFIG_SPLIT54TP_LPPS_DEAD_ZONE;
    const int64_t now = k_uptime_get();

    /* ★ 2026-11-15 (2 回目) 基準 (止まっているときの 0 の位置) を引いた値で判断する。
     *   基準は 65536 倍で持つ (256 倍だと、1 秒に 140 回読むときの 1 回分の寄せ幅が切り捨てで 0 になり、ゆっくりのほうが働かなかった) */
    const int32_t rx = (int32_t)x * 65536 - data->base_x;
    const int32_t ry = (int32_t)y * 65536 - data->base_y;
    /* 力の大きさと引き算は 256 倍 (小数点以下 8 ビット) で計算する。整数に切り捨ててから計算すると、
     * 基準が少し動いただけで値が 1 減り (9 → 8)、押し始めてすぐ 3 割遅くなった (手元の計算) */
    const int32_t qx = rx / 256;
    const int32_t qy = ry / 256;
    const int32_t dz8 = dz * 256;
    /* |qx|, |qy| ≤ 256 × 256 なので、2 乗の和は 64 ビットで */
    const int32_t m8 = (int32_t)lpps_isqrt((uint64_t)((int64_t)qx * qx + (int64_t)qy * qy));

    /* 基準からの力が小さいときだけ、基準を今の値へ寄せる (ゆっくりしたずれを追いかける) */
    int32_t dt = (data->last_read_ms == 0) ? 0 : (int32_t)MIN(now - data->last_read_ms, LPPS_BASE_DT_MAX_MS);
    data->last_read_ms = now;
    if (m8 <= LPPS_BASE_TRACK_MAX * 256 && dt > 0) {
        const int32_t tau = (m8 <= dz8) ? LPPS_BASE_TAU_MS : LPPS_BASE_TAU_SLOW_MS;
        /* 寄せるのは基準からの力が 12 以下のときだけなので |rx| < 13 × 65536 ≈ 85 万。× 250 ms ≈ 2.1 億。int32 (21 億) に収まる */
        data->base_x += rx * dt / tau;
        data->base_y += ry * dt / tau;
    }

    if (m8 <= dz8) {
        data->streak = 0;
        data->rem_x = 0;
        data->rem_y = 0;
        return false;
    }

    if (data->streak > 0 && now - data->last_above_ms > LPPS_START_GAP_MS) {
        data->streak = 0;
        data->rem_x = 0;
        data->rem_y = 0;
    }
    data->last_above_ms = now;
    if (data->streak < UINT8_MAX) {
        data->streak++;
    }
    if (data->streak < CONFIG_SPLIT54TP_LPPS_START_READS) {
        return false; /* 動き始め: まだ続いていない (1 回だけ跳ねた値かもしれない) */
    }

    /* ★ 2026-11-15 (3 回目) 速さの曲線。e = m − 引く量 (256 倍)。平らな幅 c、上がり始めの幅 r (= c)。
     *   e ≤ 1: 0 → 1 へなめらかに / 1 < e ≤ 1 + c: 1 のまま (弱い力の幅の中は同じ速さ) /
     *   1 + c < e ≤ 1 + c + r: 1 + d² / 2r (d = e − 1 − c。2 次曲線でゆっくり上がり始める) /
     *   その先: 1 + r / 2 + (d − r) (前と同じ傾き。上がり始めの終わりで傾きがちょうど 1 になり、つなぎ目で跳ねない)。
     *   平らな所の終わりで、すぐ前と同じ傾き (1 → 2 → 3) にすると、そこでまた速さが 2 倍に跳ねた (手元の計算)。
     *   CONFIG_SPLIT54TP_LPPS_PLATEAU = 0 なら e のまま (前と同じ)。 */
    const int32_t e8 = m8 - dz8;
    int32_t f8 = e8;
#if CONFIG_SPLIT54TP_LPPS_PLATEAU > 0
    {
        const int32_t s0 = 256; /* 平らな所の速さ = 1 回の読み取りで 1 */
        const int32_t c8 = CONFIG_SPLIT54TP_LPPS_PLATEAU * 256;
        const int32_t r8 = c8;
        const int32_t d8 = e8 - s0 - c8;

        if (e8 <= s0) {
            f8 = e8;
        } else if (d8 <= 0) {
            f8 = s0;
        } else if (d8 <= r8) {
            f8 = s0 + d8 * d8 / (2 * r8); /* d8 ≤ 50 × 256 なので d8² ≤ 1.7 億。int32 に収まる */
        } else {
            f8 = s0 + r8 / 2 + (d8 - r8);
        }
    }
#endif

    /* 知らせる値 (256 倍) = (qx, qy) × f / m (向きはそのまま)。qx × f8 は最大 約 6.5 万 × 9.3 万なので 64 ビットで */
    const int32_t nx = (int32_t)((int64_t)qx * f8 / m8) + data->rem_x;
    const int32_t ny = (int32_t)((int64_t)qy * f8 / m8) + data->rem_y;
    const int32_t outx = nx / 256; /* 0 の方へ切り捨て。端数は符号つきで残す */
    const int32_t outy = ny / 256;

    data->rem_x = nx - outx * 256;
    data->rem_y = ny - outy * 256;
    *ox = outx;
    *oy = outy;
    return outx != 0 || outy != 0;
}

static void lpps_motion_work_handler(struct k_work *work) {
    struct lpps_data *data = CONTAINER_OF(work, struct lpps_data, motion_work);
    const struct device *dev = data->dev;
    int ret;
    int64_t current_time = k_uptime_get();

    // Only read data if device is ready
    if (!lpps_is_ready(dev)) {
        LOG_WRN("Device not ready for motion data");
        lpps_interrupt_enable_when_inactive(data);
        return;
    }

    LOG_DBG("Motion detected at %lld ms", current_time);

    /* If behavior requested a write, perform it here (on the device workqueue) */
    if (atomic_set(&data->behavior_calibration_pending, 0)) {
        uint8_t reg = 0x04;
        uint8_t wdata[1] = {1};
        ret = lpps_i2c_write_reg(dev, reg, wdata, sizeof(wdata));
        if (ret < 0) {
            LOG_ERR("Behavior-requested I2C write failed: %d", ret);
        } else {
            LOG_DBG("Behavior-requested I2C write succeeded");
        }
#if IS_ENABLED(CONFIG_SPLIT54TP_TP_CALIB_LOCAL)
        split54tp_lpps_calib_written(ret); /* ★ 2026-11-14 校正のキーに結果を知らせる */
#endif
        if (ret >= 0) {
            /* ★ 2026-11-15 (2 回目) LPPS の 0 が変わったので、合わせ直した基準と途中の数を捨てる */
            data->base_x = 0;
            data->base_y = 0;
            data->rem_x = 0;
            data->rem_y = 0;
            data->streak = 0;
        }
    }

    uint8_t transfer_bytes[3] = {0}; /* ★ 2026-11-14 0 で初期化 */
    const struct lpps_config *cfg = dev->config;
    ret = i2c_read_dt(&cfg->i2c, transfer_bytes, sizeof(transfer_bytes));
    LOG_DBG("I2C read: data=%02X %02X %02X", transfer_bytes[0], transfer_bytes[1],
            transfer_bytes[2]);
    int8_t x = (int8_t)transfer_bytes[0];
    int8_t y = (int8_t)transfer_bytes[1];
    int8_t z = (int8_t)transfer_bytes[2];

    if (ret < 0) {
        LOG_ERR("I2C read failed: %d", ret);
        /* ★ 2026-11-14 失敗しても、バスを閉じる 1 バイトの読み取りは行う (元は飛ばしていた) */
        (void)lpps_i2c_read_reg(dev, 0, transfer_bytes, 1);
        lpps_interrupt_enable_when_inactive(data);
        return;
    }

    // close i2c bus of module
    ret = lpps_i2c_read_reg(dev, 0, transfer_bytes, 1);

    LOG_DBG("Movement: x=%4d y=%4d z=%4d", x, y, z);

    /* ★ 2026-11-14 引く前の生の値を渡す (OLED の V / Z / M、TP 校正のドリフトの判定) */
#if IS_ENABLED(CONFIG_SPLIT54TP_LPPS_RESCUE)
    split54tp_lpps_raw_rescue(x, y);
#endif
#if IS_ENABLED(CONFIG_SPLIT54TP_TP_CALIB_LOCAL)
    split54tp_lpps_raw_calib(x, y);
#endif

    /* ★ 2026-11-14 差し引いて、X も Y も 0 なら知らせない (ZMK の「操作」にも、Mac へのマウスの報告にもならない)
     * ★ 2026-11-15 力の大きさで引く・動き始めは続いてから (lpps_filter) */
    int32_t dx = 0;
    int32_t dy = 0;

    if (lpps_filter(data, x, y, &dx, &dy)) {
        input_report_rel(dev, INPUT_REL_X, dx, false, K_FOREVER);
        input_report_rel(dev, INPUT_REL_Y, dy, true, K_FOREVER);
    }

    lpps_interrupt_enable_when_inactive(data);
}

/* Public API: request that the driver perform the write on its work handler */
int lpps_request_write(const struct device *dev) {
    if (!dev) {
        return -ENODEV;
    }

    struct lpps_data *data = dev->data;
    if (!data) {
        return -ENODEV;
    }

    atomic_set(&data->behavior_calibration_pending, 1);
    return 0;
}

static void lpps_motion_handler(const struct device *gpio_dev, struct gpio_callback *cb,
                                uint32_t pins) {
    struct lpps_data *data = CONTAINER_OF(cb, struct lpps_data, motion_cb);

    ARG_UNUSED(gpio_dev);
    ARG_UNUSED(pins);

    k_work_cancel_delayable(&data->irq_reenable_work);
    lpps_interrupt_disable(data->dev);
    k_work_submit(&data->motion_work);
}

static int lpps_init(const struct device *dev) {
    const struct lpps_config *cfg = dev->config;
    struct lpps_data *data = dev->data;
    int ret;

    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("I2C bus %s is not ready", cfg->i2c.bus->name);
        return -ENODEV;
    }

    data->dev = dev;

    k_work_init(&data->motion_work, lpps_motion_work_handler);
    k_work_init_delayable(&data->irq_reenable_work, lpps_irq_reenable_work_handler);
    atomic_set(&data->behavior_calibration_pending, 0);

#if DT_INST_NODE_HAS_PROP(0, power_gpios)
    if (gpio_is_ready_dt(&cfg->power_gpio)) {
        ret = gpio_pin_configure_dt(&cfg->power_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret != 0) {
            LOG_ERR("Power pin configuration failed: %d", ret);
            return ret;
        }

        k_sleep(K_MSEC(500));

        ret = gpio_pin_set_dt(&cfg->power_gpio, 1);
        if (ret != 0) {
            LOG_ERR("Power pin set failed: %d", ret);
            return ret;
        }

        k_sleep(K_MSEC(10));
    }
#endif

    if (gpio_is_ready_dt(&cfg->irq_gpio)) {
        ret = gpio_pin_configure_dt(&cfg->irq_gpio, GPIO_INPUT);
        if (ret != 0) {
            LOG_ERR("IRQ pin configuration failed: %d", ret);
            return ret;
        }

        gpio_init_callback(&data->motion_cb, lpps_motion_handler, BIT(cfg->irq_gpio.pin));

        ret = gpio_add_callback_dt(&cfg->irq_gpio, &data->motion_cb);
        if (ret < 0) {
            LOG_ERR("Could not set motion callback: %d", ret);
            return ret;
        }

        LOG_DBG("IRQ pin configured, pin: %d", cfg->irq_gpio.pin);
    }

    if (gpio_is_ready_dt(&cfg->irq_gpio)) {
        ret = lpps_interrupt_configure(dev, GPIO_INT_LEVEL_LOW);
        if (ret != 0) {
            LOG_ERR("Motion interrupt configuration failed: %d", ret);
            return ret;
        }
    }

    ret = pm_device_runtime_enable(dev);
    if (ret < 0) {
        LOG_ERR("Failed to enable runtime power management: %d", ret);
        return ret;
    }

    return 0;
}

#ifdef CONFIG_PM_DEVICE
static int lpps_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct lpps_config *cfg = dev->config;
    struct lpps_data *data = dev->data;
    int ret;

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        k_work_cancel_delayable(&data->irq_reenable_work);
        if (gpio_is_ready_dt(&cfg->irq_gpio)) {
            ret = lpps_interrupt_configure(dev, GPIO_INT_DISABLE);
            if (ret < 0) {
                LOG_ERR("Failed to disable IRQ interrupt: %d", ret);
                return ret;
            }

            ret = gpio_pin_configure(cfg->irq_gpio.port, cfg->irq_gpio.pin,
                                     GPIO_INPUT | GPIO_PULL_DOWN);
            if (ret < 0) {
                LOG_ERR("Failed to disconnect IRQ GPIO: %d", ret);
                return ret;
            }
        }

#if DT_INST_NODE_HAS_PROP(0, power_gpios)
        if (gpio_is_ready_dt(&cfg->power_gpio)) {
            ret = gpio_pin_configure_dt(&cfg->power_gpio, GPIO_DISCONNECTED);
            if (ret < 0) {
                LOG_ERR("Failed to disconnect power: %d", ret);
                return ret;
            }
        }
#endif
        break;

    case PM_DEVICE_ACTION_RESUME:
#if DT_INST_NODE_HAS_PROP(0, power_gpios)
        if (gpio_is_ready_dt(&cfg->power_gpio)) {
            ret = gpio_pin_configure_dt(&cfg->power_gpio, GPIO_OUTPUT_ACTIVE);
            if (ret < 0) {
                LOG_ERR("Failed to enable power: %d", ret);
                return ret;
            }
            k_sleep(K_MSEC(10));
        }
#endif

        if (gpio_is_ready_dt(&cfg->irq_gpio)) {
            ret = gpio_pin_configure_dt(&cfg->irq_gpio, GPIO_INPUT);
            if (ret < 0) {
                LOG_ERR("Failed to configure IRQ GPIO: %d", ret);
                return ret;
            }

            ret = lpps_interrupt_configure(dev, GPIO_INT_LEVEL_LOW);
            if (ret < 0) {
                LOG_ERR("Failed to enable IRQ interrupt: %d", ret);
                return ret;
            }
        }

        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}
#endif

#define LPPS_INIT(n)                                                                               \
    static const struct lpps_config lpps_cfg_##n = {                                               \
        .i2c = I2C_DT_SPEC_INST_GET(n),                                                            \
        .irq_gpio = GPIO_DT_SPEC_INST_GET(n, irq_gpios),                                           \
        .power_gpio = GPIO_DT_SPEC_INST_GET_OR(n, power_gpios, {0}),                               \
    };                                                                                             \
                                                                                                   \
    static struct lpps_data lpps_data_##n;                                                         \
                                                                                                   \
    PM_DEVICE_DT_INST_DEFINE(n, lpps_pm_action);                                                   \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, lpps_init, PM_DEVICE_DT_INST_GET(n), &lpps_data_##n, &lpps_cfg_##n,   \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(LPPS_INIT)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
