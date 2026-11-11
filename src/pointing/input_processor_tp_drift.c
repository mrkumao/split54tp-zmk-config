/*
 * split54tp: スティックのドリフト (触っていないのにカーソルが動く) を捨てる input processor (2026-11-11)
 *
 *   ・背景: 「左右どちらかは不明だがドリフトがひどい。何度 TP 校正をしてもすぐ動き始める」(ユーザー、2026-11-11)。
 *     LPPS のドライバ (zmk-driver-lpps 7a4a67c) は、LPPS から読んだ値 (X / Y、符号付き 8 ビット) を、
 *     しきい値も不感帯もなくそのまま知らせる。LPPS 本体の自動の校正は中が非公開で、ドリフトの理由は不明。
 *   ・やり方 (「門」): 触っていないときのドリフトは小さな値が続くもの、使うときは大きな値が出る、とみなす。
 *       - 門が閉じている間は、X / Y の値を 0 にする (カーソルは動かない)。
 *       - 1 つの値の絶対値が open-threshold 以上なら門を開け、hold-ms の間開けておく。
 *       - 開いている間は値をそのまま通す。開いている間に keep-threshold 以上の値が来たら、また hold-ms 延ばす。
 *         (ゆっくり動かしている間に閉じないように。ドリフトの小さな値 (keep-threshold 未満) では延びない)
 *       - 指を離すと、hold-ms のあと門が閉じ、ドリフトは捨てられる。
 *     open-threshold = 0 なら何もしない (すべて通す)。
 *   ・値は「ドライバから来た生の値」で比べる。listener の input-processors のいちばん前に置くこと
 *     (感度 tp_speed の倍率をかける前。感度を変えても境目は変わらない)。
 *   ・欠点: open-threshold より弱い力で動かし始めると、境目を超えるまで動かない。
 *     ドリフトの値が open-threshold 以上あると止められない (そのときは境目を上げる)。
 *     ドリフトの大きさは、F / J (TP 復旧) の 2 行目の V で分かる (src/display/status_screen.c)。
 *   ・ZMK が「操作」と数えるのは、ドライバが出した生の知らせ (activity.c)。ここで捨てても、OLED が消えない件には効かない。
 *   ・左右のスティックの受信はどちらもセントラル (右) にあるので、これは右だけで動けばよい。
 *     状態 (門が開いている時刻) はノードごとに持つので、左右で別のノードを使う (tp_drift_l / tp_drift_r)。
 *
 * ★ 2026-11-11 (6 回目) 「差し引く」形 (dead-zone) を足した。
 *   ・背景 (ユーザー): 「細かい調整ができない。ブラウザのタブを閉じるなど、ターゲット上にうまく運べない」
 *     「カーソルが動くまでに少しタイムラグを感じる (門を入れる前はなかった)」。左右とも。
 *     門は、開いた瞬間から値をそのまま通す。右 (open 9) では、力が 9 になるまで動かず (= 遅れ)、
 *     動いた瞬間から 9 ずつ進む (= 行き過ぎる)。力を抜くと 0.3 秒後に止まり、また 9 まで押すと飛ぶ。
 *   ・やり方: X / Y それぞれ、絶対値から dead-zone を引く。dead-zone 以下は 0。
 *       例 dead-zone 6: 力 0〜6 → 0、7 → 1、8 → 2、9 → 3、20 → 14 (動き始めがゆっくり。速く動かすときはほぼ同じ)。
 *     ドリフト (dead-zone 以下の値) は、時間に関係なく常に 0 になる (門のように「開いている間は通る」ことがない)。
 *   ・軸ごとに引く (X と Y は別の知らせで来るため)。弱い力で斜めに倒すと、片方の軸だけ動くことがある。
 *     V (G / H の 2 行目) も軸ごとの最大なので、V 以上の dead-zone にすればドリフトは止まる。
 *   ・門と両方使うと、門を通った値から引く。門を使わないときは open-threshold = 0。
 *   https://zmk.dev/docs/keymaps/input-processors/usage
 *   https://github.com/sekigon-gonnoc/zmk-driver-lpps
 */

#define DT_DRV_COMPAT zmk_input_processor_tp_drift

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct tp_drift_config {
    uint16_t open_threshold;
    uint16_t keep_threshold;
    uint16_t hold_ms;
    uint16_t dead_zone; /* ★ 2026-11-11 (6 回目) */
};

struct tp_drift_data {
    int64_t open_until; /* この時刻 (k_uptime_get) まで門が開いている */
    bool was_open;      /* ログ用 */
};

static int tp_drift_handle_event(const struct device *dev, struct input_event *event,
                                 uint32_t param1, uint32_t param2,
                                 struct zmk_input_processor_state *state) {
    const struct tp_drift_config *cfg = dev->config;
    struct tp_drift_data *data = dev->data;

    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int32_t a = abs(event->value);

    if (cfg->open_threshold > 0) {
        int64_t now = k_uptime_get();
        bool open = now < data->open_until;

        if (a >= cfg->open_threshold || (open && a >= cfg->keep_threshold)) {
            data->open_until = now + cfg->hold_ms;
            open = true;
        }

        if (open != data->was_open) {
            LOG_DBG("%s: gate %s (value %d)", dev->name, open ? "open" : "closed",
                    (int)event->value);
            data->was_open = open;
        }

        if (!open) {
            /* 捨てる。知らせ自体は残す (Y には sync が付いているので、消すと X が次の sync まで残ってしまう) */
            event->value = 0;
            return ZMK_INPUT_PROC_CONTINUE;
        }
    }

    /* ★ 2026-11-11 (6 回目) 差し引く: dead-zone 以下は 0、超えた分だけ通す (符号はそのまま) */
    if (cfg->dead_zone > 0) {
        if (a <= cfg->dead_zone) {
            event->value = 0;
        } else {
            int32_t m = a - cfg->dead_zone;
            event->value = (event->value < 0) ? -m : m;
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int tp_drift_init(const struct device *dev) {
    struct tp_drift_data *data = dev->data;

    data->open_until = 0;
    data->was_open = false;
    return 0;
}

static struct zmk_input_processor_driver_api tp_drift_driver_api = {
    .handle_event = tp_drift_handle_event,
};

#define TP_DRIFT_INST(n)                                                                           \
    BUILD_ASSERT(DT_INST_PROP(n, keep_threshold) <= DT_INST_PROP(n, open_threshold) ||             \
                     DT_INST_PROP(n, open_threshold) == 0,                                         \
                 "keep-threshold must not be larger than open-threshold");                         \
    static const struct tp_drift_config tp_drift_config_##n = {                                    \
        .open_threshold = DT_INST_PROP(n, open_threshold),                                         \
        .keep_threshold = DT_INST_PROP(n, keep_threshold),                                         \
        .hold_ms = DT_INST_PROP(n, hold_ms),                                                       \
        .dead_zone = DT_INST_PROP(n, dead_zone),                                                   \
    };                                                                                          \
    static struct tp_drift_data tp_drift_data_##n;                                                 \
    DEVICE_DT_INST_DEFINE(n, tp_drift_init, NULL, &tp_drift_data_##n, &tp_drift_config_##n,        \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tp_drift_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_DRIFT_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
