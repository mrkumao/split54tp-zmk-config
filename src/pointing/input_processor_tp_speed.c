/*
 * split54tp: 倍率を動作中に変えられる scaler (2026-11-04)
 *
 *   ZMK 本体の zip_xy_scaler は倍率がビルド時に固定なので、キーで上げ下げできるように作った。
 *   計算は本体の input_processor_scaler.c と同じ (端数は track-remainders で繰り越す)。
 *     出力 = 入力 × levels[段] / 100
 *   段は &tps_l / &tps_r (behavior_tp_speed.c) から tp_speed_change() で変える。
 *   段の番号は settings の "tp_speed/<ノード名>" に保存し、電源を切っても残る。
 *   保存は最後の変更から CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE (既定 60 秒) 後
 *   (フラッシュの書き込み回数を減らすため。ZMK 本体の出力先の保存と同じやり方)。
 *   左右のスティックの受信はどちらもセントラル (右) にあるので、これは右だけで動けばよい。
 */

#define DT_DRV_COMPAT zmk_input_processor_tp_speed

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define TP_SPEED_SETTINGS_ROOT "tp_speed"

struct tp_speed_config {
    uint8_t type;
    size_t codes_len;
    const uint16_t *codes;
    size_t levels_len;
    const uint16_t *levels;
    uint8_t default_level;
};

struct tp_speed_data {
    const struct device *dev;
    uint8_t level;
#if IS_ENABLED(CONFIG_SETTINGS)
    struct k_work_delayable save_work;
#endif
};

static int tp_speed_handle_event(const struct device *dev, struct input_event *event,
                                 uint32_t param1, uint32_t param2,
                                 struct zmk_input_processor_state *state) {
    const struct tp_speed_config *cfg = dev->config;
    struct tp_speed_data *data = dev->data;

    if (event->type != cfg->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    for (size_t i = 0; i < cfg->codes_len; i++) {
        if (cfg->codes[i] != event->code) {
            continue;
        }

        // int8 の入力 × 300% でも int16 を超えるので、32 bit で計算する
        int32_t value = event->value * (int32_t)cfg->levels[data->level];

        if (state && state->remainder) {
            value += *state->remainder;
        }

        int32_t scaled = value / 100;

        if (state && state->remainder) {
            *state->remainder = (int16_t)(value - scaled * 100);
        }

        event->value = scaled;
        break;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void tp_speed_save_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct tp_speed_data *data = CONTAINER_OF(dwork, struct tp_speed_data, save_work);
    char key[40];

    snprintf(key, sizeof(key), TP_SPEED_SETTINGS_ROOT "/%s", data->dev->name);

    int err = settings_save_one(key, &data->level, sizeof(data->level));
    if (err < 0) {
        LOG_ERR("Failed to save %s (err %d)", key, err);
    }
}
#endif

/*
 * 段を変える。step = -1 (下げる) / +1 (上げる) / 0 (既定に戻す)。
 * 端の段より先へは進まない。戻り値は変更後の段。
 */
int tp_speed_change(const struct device *dev, int step) {
    const struct tp_speed_config *cfg = dev->config;
    struct tp_speed_data *data = dev->data;

    int level = (step == 0) ? cfg->default_level : data->level + step;
    level = CLAMP(level, 0, (int)cfg->levels_len - 1);

    if (level != data->level) {
        data->level = level;
#if IS_ENABLED(CONFIG_SETTINGS)
        k_work_reschedule(&data->save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#endif
    }

    LOG_INF("%s: level %d (%d%%)", dev->name, level, cfg->levels[level]);

    return level;
}

static int tp_speed_init(const struct device *dev) {
    struct tp_speed_data *data = dev->data;

    // 段は data の初期値 (default-level) か、settings から読んだ値のまま触らない
    data->dev = dev;
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_init_delayable(&data->save_work, tp_speed_save_work);
#endif

    return 0;
}

static struct zmk_input_processor_driver_api tp_speed_driver_api = {
    .handle_event = tp_speed_handle_event,
};

#define TP_SPEED_INST(n)                                                                           \
    BUILD_ASSERT(DT_INST_PROP(n, default_level) < DT_INST_PROP_LEN(n, levels),                     \
                 "default-level must be an index of levels");                                      \
    static const uint16_t tp_speed_codes_##n[] = DT_INST_PROP(n, codes);                           \
    static const uint16_t tp_speed_levels_##n[] = DT_INST_PROP(n, levels);                         \
    static const struct tp_speed_config tp_speed_config_##n = {                                    \
        .type = DT_INST_PROP(n, type),                                                             \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .codes = tp_speed_codes_##n,                                                               \
        .levels_len = DT_INST_PROP_LEN(n, levels),                                                 \
        .levels = tp_speed_levels_##n,                                                             \
        .default_level = DT_INST_PROP(n, default_level),                                           \
    };                                                                                             \
    static struct tp_speed_data tp_speed_data_##n = {                                              \
        .level = DT_INST_PROP(n, default_level),                                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, tp_speed_init, NULL, &tp_speed_data_##n, &tp_speed_config_##n,        \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tp_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_SPEED_INST)

#if IS_ENABLED(CONFIG_SETTINGS)
#define TP_SPEED_DEV_PTR(n) DEVICE_DT_INST_GET(n),

static const struct device *const tp_speed_devs[] = {DT_INST_FOREACH_STATUS_OKAY(TP_SPEED_DEV_PTR)};

static int tp_speed_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    for (size_t i = 0; i < ARRAY_SIZE(tp_speed_devs); i++) {
        const struct device *dev = tp_speed_devs[i];
        const char *next;

        if (!settings_name_steq(name, dev->name, &next) || next) {
            continue;
        }

        const struct tp_speed_config *cfg = dev->config;
        struct tp_speed_data *data = dev->data;
        uint8_t level;

        if (len != sizeof(level)) {
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &level, sizeof(level));
        if (rc < 0) {
            return rc;
        }

        // levels の段数を減らしたあとに古い値が残っていても、範囲外なら使わない
        if (level < cfg->levels_len) {
            data->level = level;
        }

        return 0;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(tp_speed, TP_SPEED_SETTINGS_ROOT, NULL, tp_speed_settings_set, NULL,
                               NULL);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
