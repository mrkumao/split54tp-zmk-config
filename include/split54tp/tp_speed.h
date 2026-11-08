/*
 * split54tp: スティックの感度 (src/pointing/input_processor_tp_speed.c) の外向けの口 (2026-11-08)
 *
 *   ・tp_speed_change()      段を変える (behavior_tp_speed.c から)
 *   ・tp_speed_get_percent() 今の倍率 (%) を読む (OLED の画面 src/display/status_screen.c から)
 *   ・split54tp_tp_speed_changed  段が変わったとき (と、保存した段を読み込んだとき) に出すイベント。
 *     画面はこれを受けて表示を直す。
 *   セントラル (右) のビルドだけで使う (CMakeLists.txt)。
 */

#pragma once

#include <zephyr/device.h>
#include <zmk/event_manager.h>

int tp_speed_change(const struct device *dev, int step);
int tp_speed_get_percent(const struct device *dev);

struct split54tp_tp_speed_changed {
    const struct device *dev;
    uint16_t percent;
};

ZMK_EVENT_DECLARE(split54tp_tp_speed_changed);
