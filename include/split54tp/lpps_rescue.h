/*
 * split54tp: LPPS の読み直し (src/pointing/lpps_rescue.c) の外向けの口 (2026-11-11)
 *
 *   ・split54tp_lpps_reread()  ドライバが MOTION のたびにするのと同じ読み取り (3 バイト → バスを閉じる 1 バイト) をする。
 *     ⚠ system work queue の中から呼ぶこと。ドライバの読み取り (motion_work) と同じ queue にして、同時に読まないようにしている。
 *     TP 復旧のキー (behavior_tp_fix.c) と、自動の読み直し (lpps_rescue.c) が使う。
 *   左右どちらのビルドにも入る。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct split54tp_lpps_reread_result {
    int8_t motion_before; /* MOTION の線: 0 = Low (LPPS が「読んで」と言っている) / 1 = High / -1 = 読めない */
    int8_t motion_after;  /* 読み取りのあと (check_after が false なら -1) */
    int16_t read_ret;     /* 3 バイトの読み取りの結果 (0 = 成功、負 = エラー番号) */
    int16_t close_ret;    /* バスを閉じる 1 バイトの読み取りの結果 */
};

/* check_after = true なら、読み取りのあと少し待ってから MOTION をもう一度読む (表示用) */
void split54tp_lpps_reread(struct split54tp_lpps_reread_result *r, bool check_after);
