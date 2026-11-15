/*
 * split54tp: BT のプロファイルごとに OS のレイヤーを合わせる (2026-11-15)
 *
 *   ユーザー指定: OS を Mac / Windows / iOS / Android / Linux の 5 つに、BT も 5 つに (BT1〜5 と OS は 1 対 1。将来は別に選びたい)。
 *   キーマップの os_layers ノード (compatible "zmk,split54tp-os-layers") の profile-layers に、
 *   BT のプロファイルの番号 (0 から) ごとの OS のレイヤーを書く。
 *
 *   なぜ要るか: ZMK はレイヤーの状態を保存しないので、電源を入れた直後は Base だけになる。
 *   今まではプロファイルを選ぶキー (Ctrl + 1〜5) を押すまで OS のレイヤーにならなかった
 *   (Mac につながっていても Base = Windows の並び)。Windows も OS のレイヤーを持つようにしたので (左 Space の長押し = Win)、
 *   つながった相手に合わせて自動で OS のレイヤーにする。
 *
 *   動き: ZMK の「選んでいるプロファイルが変わった / その相手とつながった・切れた」の知らせ
 *   (zmk_ble_active_profile_changed。app/src/ble.c が、選んだとき・つながったとき・切れたときに出す) を受けて、
 *     ・USB で使っているときは何もしない (今使っている出力が USB なら)
 *     ・そのプロファイルの OS のレイヤーがもう有効なら何もしない (つながり直すたびに L1 などが外れないように)
 *     ・そうでなければ、ほかの OS のレイヤーだけを外して、その OS のレイヤーを有効にする
 *       (&to と違い、L1〜L3・設定レイヤーなどは触らない)
 *   キーの prof1_mac〜prof5_linux / pair1〜pair5 の &to はそのまま (先にこれが動き、&to は同じ結果になる)。
 *
 *   ZMK 641514a: app/include/zmk/keymap.h (zmk_keymap_layer_activate / deactivate / active)、
 *   app/include/zmk/endpoints.h (zmk_endpoint_get_selected)
 */

#define DT_DRV_COMPAT zmk_split54tp_os_layers

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static const uint8_t profile_layers[] = DT_INST_PROP(0, profile_layers);

static int os_layers_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev = as_zmk_ble_active_profile_changed(eh);
    if (ev == NULL || ev->index >= ARRAY_SIZE(profile_layers)) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    /* USB で使っているときは何もしない。preferred (USB / BT の設定) ではなく、今使っている出力で見る:
     * ZMK の preferred の初期値は USB で、BT だけで使っていても USB のままのことがある (app/src/endpoints.c)。
     * つながった直後は selected がまだ NONE のことがあるが、USB でなければ合わせる */
    if (zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const zmk_keymap_layer_id_t target = profile_layers[ev->index];
    if (zmk_keymap_layer_active(target)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (size_t i = 0; i < ARRAY_SIZE(profile_layers); i++) {
        if (profile_layers[i] != target && zmk_keymap_layer_active(profile_layers[i])) {
            zmk_keymap_layer_deactivate(profile_layers[i], false);
        }
    }
    zmk_keymap_layer_activate(target, false);
    LOG_INF("os_layers: profile %u -> layer %u", (unsigned int)ev->index, (unsigned int)target);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split54tp_os_layers, os_layers_listener);
ZMK_SUBSCRIPTION(split54tp_os_layers, zmk_ble_active_profile_changed);

#endif
