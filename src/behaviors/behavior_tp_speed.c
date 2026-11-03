/*
 * split54tp: スティックの感度を 1 段ずつ上げ下げする behavior (2026-11-04)
 *
 *   processor = <&tp_speed_l> のように対象の tp-speed (input_processor_tp_speed.c) を指定する。
 *   param1: 0 = 下げる / 1 = 上げる / 2 = 既定に戻す (キーマップの TPS_DN / TPS_UP / TPS_DEF)
 *   左右のスティックの受信はどちらもセントラル (右) にあるので、locality は既定 (セントラル) のまま。
 */

#define DT_DRV_COMPAT zmk_behavior_tp_speed

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* src/pointing/input_processor_tp_speed.c */
extern int tp_speed_change(const struct device *dev, int step);

struct behavior_tp_speed_config {
    const struct device *processor;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata tp_speed_values[] = {
    {
        .value = 0,
        .display_name = "Slower",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
    {
        .value = 1,
        .display_name = "Faster",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
    {
        .value = 2,
        .display_name = "Default",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
};

static const struct behavior_parameter_metadata_set tp_speed_set = {
    .param1_values = tp_speed_values,
    .param1_values_len = ARRAY_SIZE(tp_speed_values),
};

static const struct behavior_parameter_metadata tp_speed_metadata = {
    .sets_len = 1,
    .sets = &tp_speed_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int on_tp_speed_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_tp_speed_config *cfg = dev->config;
    int step;

    switch (binding->param1) {
    case 0:
        step = -1;
        break;
    case 1:
        step = 1;
        break;
    case 2:
        step = 0;
        break;
    default:
        LOG_ERR("Unknown tp speed command: %d", binding->param1);
        return -ENOTSUP;
    }

    tp_speed_change(cfg->processor, step);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_tp_speed_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_tp_speed_driver_api = {
    .binding_pressed = on_tp_speed_pressed,
    .binding_released = on_tp_speed_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &tp_speed_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define TP_SPEED_BEHAVIOR_INST(n)                                                                  \
    static const struct behavior_tp_speed_config behavior_tp_speed_config_##n = {                  \
        .processor = DEVICE_DT_GET(DT_INST_PHANDLE(n, processor)),                                 \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_tp_speed_config_##n, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_SPEED_BEHAVIOR_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
