/*
 * split54tp: 取り込んだ LPPS のドライバ (src/pointing/lpps.c) と、自作の部分をつなぐ口 (2026-11-14)
 *
 *   ドライバは、入力の知らせ (input_report_rel) を差し引いたあとの値でしか出さない (X も Y も 0 なら出さない)。
 *   ドリフトの大きさの表示や TP 校正の判定には、引く前の生の値が要るので、読み取りのたびにここの関数を呼ぶ。
 *   どれもドライバの読み取り (system work queue) の中から呼ばれる。I2C は使わないこと。
 *   keyboard-project/docs/rev7-修正計画.md 手順 205。
 */

#pragma once

#include <stdint.h>

#include <zephyr/device.h>

/* ドライバの公開関数 (上流と同じ)。次にドライバが値を読む直前に、校正 (レジスタ 0x04 に 1) を書く */
int lpps_request_write(const struct device *dev);

/* 読み取りのたびの生の値 (X / Y)。src/pointing/lpps_rescue.c (CONFIG_SPLIT54TP_LPPS_RESCUE) が受ける */
void split54tp_lpps_raw_rescue(int8_t x, int8_t y);

/* 読み取りのたびの生の値 (X / Y)。src/behaviors/behavior_tp_calib_local.c (CONFIG_SPLIT54TP_TP_CALIB_LOCAL) が受ける */
void split54tp_lpps_raw_calib(int8_t x, int8_t y);

/* 校正を書いた結果 (0 = 成功、負 = エラー番号)。src/behaviors/behavior_tp_calib_local.c が受ける */
void split54tp_lpps_calib_written(int ret);
