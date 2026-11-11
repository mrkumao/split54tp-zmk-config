/*
 * split54tp: TP 復旧のキー (src/behaviors/behavior_tp_fix.c) の結果を知らせるイベント (2026-11-11)
 *
 *   押した側の LPPS に、ドライバが MOTION のたびにするのと同じ読み取り
 *   (3 バイト読む → バスを閉じるために 1 バイト読む) を送った結果。
 *   OLED の画面 (src/display/status_screen.c) がこれを受けて、TP の行に 10 秒出す。
 *   左右どちらのビルドにも入る (押した側で動くため)。
 */

#pragma once

#include <zmk/event_manager.h>

struct split54tp_tp_fix_done {
    int8_t motion_before; /* MOTION の線: 0 = Low (LPPS が「読んで」と言っている) / 1 = High / -1 = 読めない */
    int8_t motion_after;  /* 読み取りのあと */
    int16_t read_ret;     /* 3 バイトの読み取りの結果 (0 = 成功、負 = エラー番号) */
    int16_t close_ret;    /* バスを閉じる 1 バイトの読み取りの結果 */
    /* ★ 2026-11-11 前に押してから今までの、LPPS の入力の通知の数 (値 0 / 0 でない)。OLED が消えない原因の確認用 */
    uint32_t zero_events;
    uint32_t move_events;
    /* ★ 2026-11-11 (3 回目) 同じ間の、動きの値の最大 (X / Y の絶対値、ドライバから来た生の値)。ドリフトの大きさの確認用 */
    uint32_t max_value;
    /* ★ 2026-11-11 前に押してから今までの、ZMK が「操作」と数えるものの数 (src/display/activity_count.c) */
    uint32_t key_events;    /* キーを押した数 (今押した F / J も入る) */
    uint32_t sensor_events; /* ホイールの知らせ */
    uint32_t input_events;  /* 入力の知らせ (すべての入力の装置。右では左のスティックの分も入る) */
};

/* 前に呼んでから今までの、キー / ホイール / 入力の知らせの数。呼ぶと 0 に戻す (src/display/activity_count.c) */
void split54tp_activity_take_counts(uint32_t *keys, uint32_t *sensors, uint32_t *inputs);

ZMK_EVENT_DECLARE(split54tp_tp_fix_done);
