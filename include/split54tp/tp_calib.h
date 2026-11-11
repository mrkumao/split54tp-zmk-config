/*
 * split54tp: TP 校正のキー (src/behaviors/behavior_tp_calib_local.c) の結果を知らせるイベント (2026-11-11)
 *
 *   OLED の画面 (src/display/status_screen.c) がこれを受けて、押した側の TP の行に 10 秒出す。
 *   左右どちらのビルドにも入る (押した側で動くため)。
 */

#pragma once

#include <zmk/event_manager.h>

#define SPLIT54TP_CALIB_DONE 0    /* 校正した (頼んだあと、ドライバが次の値を読んだ) */
#define SPLIT54TP_CALIB_SKIP 1    /* 待つ間にスティックから値が来なかった (ドリフトしていない) ので、校正しなかった */
#define SPLIT54TP_CALIB_PENDING 2 /* 頼んだが、まだ行われていない (次にスティックに力がかかったときに行われてしまう) */
#define SPLIT54TP_CALIB_ERROR 3   /* ドライバに頼めなかった */

struct split54tp_tp_calib_done {
    uint8_t result;  /* SPLIT54TP_CALIB_* */
    uint16_t peak;   /* 待つ間に来た値の最大 (X / Y の絶対値、ドライバから来た生の値) */
    uint16_t count;  /* 待つ間に来た 0 でない値の数 (X / Y を別に数える) */
};

ZMK_EVENT_DECLARE(split54tp_tp_calib_done);
