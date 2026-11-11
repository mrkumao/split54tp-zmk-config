/*
 * split54tp: OLED の表示の内容を右 (セントラル) から左 (ペリフェラル) へ送る形 (2026-11-09)
 *
 *   ZMK の「左で behavior を動かす」仕組み (zmk_split_central_invoke_behavior) で送れるのは
 *   behavior の名前と 2 つの 32 bit の数 (param1 / param2) だけなので、表示の内容をそこに詰める。
 *   param1 の上 8 bit を種類 (OLEDSYNC_OP_*) に使う。
 *
 *   OLEDSYNC_OP_STATE: param1 = 下の表 / param2 = TP 左 % (下 16 bit) と TP 右 % (上 16 bit)
 *     bit 0〜1  出力先 (enum zmk_transport: 0 = なし、1 = USB、2 = BT)
 *     bit 2     出力先がつながっている
 *     bit 3〜5  BT のプロファイル番号 (0 から)
 *     bit 6     BT がつながっている
 *     bit 7     BT が登録済み
 *     bit 8〜15 修飾キー (MOD_LCTL など。左右別)
 *     bit 16    Caps Lock
 *     bit 17    Opt / Cmd の表記 (Mac・iPad のレイヤーが有効)
 *     bit 18〜21 スクロールの速さの段 + 1 (1〜10。0 = 不明)。★ 2026-11-11
 *   OLEDSYNC_OP_LAYER: レイヤー名の 7 文字まで。param1 の下 24 bit に 0〜2 文字目、param2 に 3〜6 文字目
 *
 *   右: src/display/status_screen.c (送る) / 左: src/behaviors/behavior_oled_sync.c (受ける) → status_screen.c
 */

#pragma once

#include <stdint.h>

/* devicetree のノード名 (split54tp.dtsi)。左で動かす behavior の名前は 8 文字以内 */
#define OLEDSYNC_BEHAVIOR_NAME "oledsync"

#define OLEDSYNC_OP_STATE 0x01
#define OLEDSYNC_OP_LAYER 0x02

#define OLEDSYNC_OP(p1) ((uint8_t)((p1) >> 24))

#define OLEDSYNC_LAYER_NAME_LEN 7

/* 左: 右から届いた内容を画面へ渡す (behavior_oled_sync.c から呼ぶ) */
void split54tp_status_apply_remote(uint32_t param1, uint32_t param2);
