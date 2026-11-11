/*
 * split54tp: ZMK が「操作」と数えるものの数 (確認用、2026-11-11)
 *
 *   ・背景: 何も触らずに 10 分置いても OLED が消えない (keyboard-project/docs/rev7-修正計画.md 手順 174)。
 *     ZMK (app/src/activity.c) が「操作」と数えるのは、キー (zmk_position_state_changed)・ホイール (zmk_sensor_event)・
 *     入力の通知 (INPUT_CALLBACK_DEFINE(NULL, ...) = すべての入力の装置) の 3 つ。どれかが来続けると OLED が消えない。
 *   ・ここでは同じ 3 つを数える。TP 復旧のキー (F / J、behavior_tp_fix.c) を押すと、前に押してからの数を
 *     OLED の修飾キーの段に 10 秒出す (status_screen.c)。K = キーを押した数 (今押した F / J も入る)、
 *     S = ホイールの知らせ、I = 入力の知らせ (右では左のスティックから届いた分も入る)。
 *   左右どちらのビルドにも入る (CONFIG_SPLIT54TP_TP_FIX)。
 *   https://github.com/zmkfirmware/zmk/blob/main/app/src/activity.c
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/input/input.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>

#include <split54tp/tp_fix.h>

static atomic_t key_count;
static atomic_t sensor_count;
static atomic_t input_count;

void split54tp_activity_take_counts(uint32_t *keys, uint32_t *sensors, uint32_t *inputs) {
    *keys = (uint32_t)atomic_clear(&key_count);
    *sensors = (uint32_t)atomic_clear(&sensor_count);
    *inputs = (uint32_t)atomic_clear(&input_count);
}

static int activity_count_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pev = as_zmk_position_state_changed(eh);
    if (pev != NULL) {
        if (pev->state) {
            atomic_inc(&key_count);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (as_zmk_sensor_event(eh) != NULL) {
        atomic_inc(&sensor_count);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split54tp_activity_count, activity_count_listener);
ZMK_SUBSCRIPTION(split54tp_activity_count, zmk_position_state_changed);
ZMK_SUBSCRIPTION(split54tp_activity_count, zmk_sensor_event);

static void activity_count_input(struct input_event *evt, void *user_data) {
    ARG_UNUSED(evt);
    ARG_UNUSED(user_data);
    atomic_inc(&input_count);
}

INPUT_CALLBACK_DEFINE(NULL, activity_count_input, NULL);
