/*
 * split54tp: OLED の自作の画面 (2026-11-08 右 / 2026-11-09 左にも)
 *
 *   128 x 64 の配置 (y は上から):
 *     y  0〜   1 行目 (Montserrat 12)  左 = BT の番号と接続 / 中 = CAPS (点灯中だけ白抜き) / 右 = 電池 %
 *     y 13〜41 大きく (Montserrat 26)  接続先の名前 (Mac / Win / iPad / Spare / USB)。右に今のレイヤー名
 *     y 44〜54 修飾キー 4 つ (UNSCII 8) Sft / Ctl / Alt / Win (Mac・iPad のレイヤーのときは Opt / Cmd)。押している間だけ白抜き
 *     y 55〜63 TP の感度 (UNSCII 8)     左・右の %
 *
 *   ・右 (セントラル) は自分で状態を集めて画面に出し、同じ内容を左へ送る。
 *     送り方: ZMK の「左で behavior を動かす」仕組み (zmk_split_central_invoke_behavior) で、
 *     左の受け口 "oledsync" (src/behaviors/behavior_oled_sync.c) に 2 つの数を渡す (include/split54tp/status_sync.h)。
 *     変わったときに送るほか、BT がつながったとき (左がつながり直したときを含む) に 3 回送り直す。
 *   ・左 (ペリフェラル) は右から届いた内容を出す。電池 % は左自身のもの。右とつながっていないときは「R ✕」と「----」。
 *     USB の給電中 (⚡) は左右とも出す。条件は ZMK 標準の電池の表示 (widgets/battery_status.c) と同じ
 *     CONFIG_USB_DEVICE_STACK (★ 2026-11-09 修正: 最初は CONFIG_ZMK_USB (右だけ) にしていて、左で ⚡ が出なかった)。
 *   ・接続先の名前は BT のプロファイル番号から決める (キーマップの prof1_mac など: 1 = Mac、2 = Win、3 = iPad、4 = 予備)。
 *     番号と名前の対応を変えたら PROFILE_NAMES も直すこと。
 *   ・Opt / Cmd にするかは「Mac」「iPad」という名前のレイヤーが有効かで決める (キーマップの display-name)。
 *   ・Caps Lock はパソコンから届くランプの状態 (右の CONFIG_ZMK_HID_INDICATORS)。選んでいる接続先の分を出す。
 *   ・修飾キーは左右どちらでも同じ枠が光る。
 *   ・I2C は LPPS と同じ線なので、表示が変わったときだけ書き直す (ふつうの文字のキーでは書かない)。
 *
 *   ★ 2026-11-09 右: 左とつながっていないとき、大きい文字の右上 (レイヤー名の上、y 12) に「L ✕」を出す。
 *     右が BT の central 側になっている接続 = 左との接続 (パソコンとの接続では右は peripheral 側) を数えて判断する。
 *     1 行目は「BT5 ⚙ L✕」(約 57) と電池「⚡100%」(約 38) の間が 33 しかなく CAPS (36) と重なるので、ここに置いた。
 *     レイヤー名は y 24 → 26 に下げた。
 *
 *   ★ 2026-11-09 右端の太い白い縦線を直した: TP の行の文字 (UNSCII 8) は高さが 9 ドットで、y 56 に置くと画面の下を
 *     1 ドットはみ出し、画面がスクロールできる扱いになって白黒テーマのスクロールバー (白い太線) が出ていた。
 *     TP の行を y 55 に上げ、画面のスクロールとスクロールバーを止めた。
 *
 *   ★ 2026-11-11 TP 復旧のキー (src/behaviors/behavior_tp_fix.c、設定レイヤーの F / J) を押すと、押した側の
 *     TP の行に結果を 10 秒出す: 「FIX 前>後 ok」または「FIX 前>後 読み取り バスを閉じる」(エラー番号)。
 *     前 / 後 = 読み取りの前後の MOTION の線 (L = Low = LPPS が「読んで」と言っている / H = High / ? = 読めない)。
 *     1 行 16 文字 (UNSCII 8 で 128 ドット) を超えないように切る。この表示は左へは送らない (押した側だけ)。
 *
 *   作り方は ZMK 標準の画面 (app/src/display/status_screen.c と widgets/) と同じ:
 *   ZMK のイベントで状態を集め、表示の作業は表示専用のスレッド (zmk_display_work_q) で行う。
 *   https://github.com/zmkfirmware/zmk/tree/main/app/src/display
 */

#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>
#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/endpoints_types.h>

#include <split54tp/status_sync.h>

/* 右 (セントラル) か、左 (ペリフェラル) か */
#define STATUS_CENTRAL (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
#define STATUS_PERIPHERAL (IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
/* 右が左へ送る */
#define STATUS_SENDS_TO_PERIPHERAL (IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#if STATUS_CENTRAL
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>
#endif
#endif /* STATUS_CENTRAL */

#if STATUS_SENDS_TO_PERIPHERAL
#include <zephyr/bluetooth/conn.h>
#include <zmk/behavior.h>
#include <zmk/split/central.h>
#endif

#if STATUS_PERIPHERAL
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#endif

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#endif

/* TP の感度 (src/pointing/input_processor_tp_speed.c、右だけ)。split54tp.dtsi の tp_speed_l / tp_speed_r */
#define TP_SPEED_SHOWN                                                                             \
    (STATUS_CENTRAL && IS_ENABLED(CONFIG_SPLIT54TP_TP_SPEED) &&                                    \
     DT_NODE_HAS_STATUS(DT_NODELABEL(tp_speed_l), okay) &&                                         \
     DT_NODE_HAS_STATUS(DT_NODELABEL(tp_speed_r), okay))

#if TP_SPEED_SHOWN
#include <split54tp/tp_speed.h>
static const struct device *const tp_dev_l = DEVICE_DT_GET(DT_NODELABEL(tp_speed_l));
static const struct device *const tp_dev_r = DEVICE_DT_GET(DT_NODELABEL(tp_speed_r));
#endif

/* TP 復旧のキー (2026-11-11)。押した側で動き、結果をイベントで知らせる */
#if IS_ENABLED(CONFIG_SPLIT54TP_TP_FIX)
#include <split54tp/tp_fix.h>
#endif
#define TP_LINE_CHARS 16      /* TP の行に出せる文字数 (UNSCII 8 は 1 文字 8 ドット × 16 = 128) */
#define TP_FIX_SHOW_MS 10000 /* TP 復旧の結果を出しておく時間 */

/* BT のプロファイル番号 (0 から) → 大きく出す名前。キーマップの prof1_mac〜prof4_spare と合わせる */
static const char *const PROFILE_NAMES[] = {"Mac", "Win", "iPad", "Spare"};

#if STATUS_CENTRAL
/* Opt / Cmd の表記にするレイヤーの名前 (キーマップの display-name) */
static const char *const MAC_LAYER_NAMES[] = {"Mac", "iPad"};
#endif

/* Caps Lock のランプのビット (HID の LED のページ: ビット 0 = Num Lock、ビット 1 = Caps Lock) */
#define CAPS_LOCK_BIT BIT(HID_USAGE_LED_CAPS_LOCK - 1)

/* ---------------------------------------------------------------- 状態 */

struct status_state {
    /* 右で集める (左は右から受け取る) */
    uint8_t transport;        /* enum zmk_transport。つながっていなければ、つなごうとしている出力先 */
    bool transport_connected; /* 出力先がつながっている */
    uint8_t profile;          /* BT のプロファイル番号 (0 から) */
    bool ble_connected;
    bool ble_bonded; /* 登録済み (false = ペアリング待ち) */
    uint8_t mods;    /* MOD_LCTL など */
    bool caps;
    bool mac_style;
    uint16_t tp_l; /* 0 = 不明 */
    uint16_t tp_r;
    char layer_name[OLEDSYNC_LAYER_NAME_LEN + 1];
    /* 自分の */
    uint8_t battery;
    bool usb_powered;
    /* 左だけ */
    bool link_connected; /* 右とつながっている */
    bool remote_valid;   /* 右から内容が届いている */
    /* 自分の: TP 復旧のキーの結果 (出している間だけ fix_active) */
    bool fix_active;
    char fix_text[TP_LINE_CHARS + 1];
};

/* 右から左へ送る項目が同じか */
static bool shared_equal(const struct status_state *a, const struct status_state *b) {
    return a->transport == b->transport && a->transport_connected == b->transport_connected &&
           a->profile == b->profile && a->ble_connected == b->ble_connected &&
           a->ble_bonded == b->ble_bonded && a->mods == b->mods && a->caps == b->caps &&
           a->mac_style == b->mac_style && a->tp_l == b->tp_l && a->tp_r == b->tp_r;
}

static bool state_equal(const struct status_state *a, const struct status_state *b) {
    return shared_equal(a, b) && strcmp(a->layer_name, b->layer_name) == 0 &&
           a->battery == b->battery && a->usb_powered == b->usb_powered &&
           a->link_connected == b->link_connected && a->remote_valid == b->remote_valid &&
           a->fix_active == b->fix_active && strcmp(a->fix_text, b->fix_text) == 0;
}

#if STATUS_PERIPHERAL
/* 左: 右から届いた内容 */
K_MUTEX_DEFINE(remote_mutex);
static struct status_state remote;
static bool remote_valid;
#endif

#if STATUS_SENDS_TO_PERIPHERAL
/* 右: 左とつながっている数 (右が BT の central 側になっている接続の数) */
static atomic_t left_links;
#endif

static void status_refresh(const zmk_event_t *eh);

/* TP 復旧のキーの結果 (押した側の画面にだけ、TP_FIX_SHOW_MS の間出す) */
K_MUTEX_DEFINE(fix_mutex);
static bool fix_active;
static char fix_text[TP_LINE_CHARS + 1];

#if IS_ENABLED(CONFIG_SPLIT54TP_TP_FIX)
static void fix_clear_work_cb(struct k_work *work) {
    k_mutex_lock(&fix_mutex, K_FOREVER);
    fix_active = false;
    k_mutex_unlock(&fix_mutex);
    status_refresh(NULL);
}

K_WORK_DELAYABLE_DEFINE(fix_clear_work, fix_clear_work_cb);

static char motion_char(int8_t level) { return (level < 0) ? '?' : (level ? 'H' : 'L'); }

/* 例: "FIX L>H ok" / "FIX H>H -5 -5" (16 文字で切る) */
static void fix_store(const struct split54tp_tp_fix_done *f) {
    char text[32];
    if (f->read_ret == 0 && f->close_ret == 0) {
        snprintf(text, sizeof(text), "FIX %c>%c ok", motion_char(f->motion_before),
                 motion_char(f->motion_after));
    } else {
        snprintf(text, sizeof(text), "FIX %c>%c %d %d", motion_char(f->motion_before),
                 motion_char(f->motion_after), f->read_ret, f->close_ret);
    }

    k_mutex_lock(&fix_mutex, K_FOREVER);
    strncpy(fix_text, text, TP_LINE_CHARS);
    fix_text[TP_LINE_CHARS] = '\0';
    fix_active = true;
    k_mutex_unlock(&fix_mutex);

    k_work_reschedule(&fix_clear_work, K_MSEC(TP_FIX_SHOW_MS));
}
#endif /* CONFIG_SPLIT54TP_TP_FIX */

#if STATUS_CENTRAL
static bool mac_layer_active(void) {
    for (zmk_keymap_layer_index_t i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(i);
        if (id == ZMK_KEYMAP_LAYER_ID_INVAL || !zmk_keymap_layer_active(id)) {
            continue;
        }
        const char *name = zmk_keymap_layer_name(id);
        if (name == NULL) {
            continue;
        }
        for (size_t n = 0; n < ARRAY_SIZE(MAC_LAYER_NAMES); n++) {
            if (strcmp(name, MAC_LAYER_NAMES[n]) == 0) {
                return true;
            }
        }
    }
    return false;
}
#endif

/*
 * 今の状態を集める (イベントの処理の中、または表示専用のスレッドで呼ばれる)。
 * 右の修飾キーは、ZMK 本体が HID の状態を直すのと、この画面がイベントを受けるのと、どちらが先か決まっていないので、
 * 今の HID の状態に、受けたイベントの修飾キーの押す / 離すを重ねる (どちらの順でも同じ結果になる)。
 */
static struct status_state gather_state(const zmk_event_t *eh) {
    struct status_state s;
    memset(&s, 0, sizeof(s));

#if STATUS_CENTRAL
    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    s.transport_connected = selected.transport != ZMK_TRANSPORT_NONE;
    s.transport = (uint8_t)(s.transport_connected ? selected.transport
                                                  : zmk_endpoint_get_preferred_transport());

#if IS_ENABLED(CONFIG_ZMK_BLE)
    s.profile = (uint8_t)zmk_ble_active_profile_index();
    s.ble_connected = zmk_ble_active_profile_is_connected();
    s.ble_bonded = !zmk_ble_active_profile_is_open();
#endif

    zmk_keymap_layer_index_t layer_index = zmk_keymap_highest_layer_active();
    const char *layer_name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(layer_index));
    if (layer_name != NULL && layer_name[0] != '\0') {
        strncpy(s.layer_name, layer_name, OLEDSYNC_LAYER_NAME_LEN);
    } else {
        snprintf(s.layer_name, sizeof(s.layer_name), "L%u", (unsigned int)layer_index);
    }
    s.mac_style = mac_layer_active();

    s.mods = zmk_hid_get_explicit_mods();
    const struct zmk_keycode_state_changed *kev =
        (eh != NULL) ? as_zmk_keycode_state_changed(eh) : NULL;
    if (kev != NULL && kev->usage_page == HID_USAGE_KEY &&
        kev->keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
        kev->keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
        WRITE_BIT(s.mods, kev->keycode - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, kev->state);
    }

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    s.caps = (zmk_hid_indicators_get_current_profile() & CAPS_LOCK_BIT) != 0;
#endif

#if TP_SPEED_SHOWN
    s.tp_l = (uint16_t)tp_speed_get_percent(tp_dev_l);
    s.tp_r = (uint16_t)tp_speed_get_percent(tp_dev_r);
#endif

#if STATUS_SENDS_TO_PERIPHERAL
    s.link_connected = atomic_get(&left_links) > 0;
#else
    s.link_connected = true;
#endif
#endif /* STATUS_CENTRAL */

#if STATUS_PERIPHERAL
    k_mutex_lock(&remote_mutex, K_FOREVER);
    if (remote_valid) {
        s.transport = remote.transport;
        s.transport_connected = remote.transport_connected;
        s.profile = remote.profile;
        s.ble_connected = remote.ble_connected;
        s.ble_bonded = remote.ble_bonded;
        s.mods = remote.mods;
        s.caps = remote.caps;
        s.mac_style = remote.mac_style;
        s.tp_l = remote.tp_l;
        s.tp_r = remote.tp_r;
        memcpy(s.layer_name, remote.layer_name, sizeof(s.layer_name));
    }
    s.remote_valid = remote_valid;
    k_mutex_unlock(&remote_mutex);

    const struct zmk_split_peripheral_status_changed *pev =
        (eh != NULL) ? as_zmk_split_peripheral_status_changed(eh) : NULL;
    s.link_connected = (pev != NULL) ? pev->connected : zmk_split_bt_peripheral_is_connected();
#endif /* STATUS_PERIPHERAL */

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    const struct zmk_battery_state_changed *bev =
        (eh != NULL) ? as_zmk_battery_state_changed(eh) : NULL;
    s.battery = (bev != NULL) ? bev->state_of_charge : zmk_battery_state_of_charge();
#endif

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    s.usb_powered = zmk_usb_is_powered();
#endif

    k_mutex_lock(&fix_mutex, K_FOREVER);
    s.fix_active = fix_active;
    if (fix_active) {
        memcpy(s.fix_text, fix_text, sizeof(s.fix_text));
    }
    k_mutex_unlock(&fix_mutex);

    return s;
}

/* ---------------------------------------------------------------- 画面 */

#define MOD_BOX_COUNT 4
#define MOD_BOX_W 30
#define MOD_BOX_H 11 /* 枠 1 + 上の余白 1 + 文字 8 + 枠 1。y 44〜54 */
#define MOD_BOX_GAP 2
#define MOD_BOX_Y 44
#define TP_Y 55 /* UNSCII 8 の行の高さは 9 → y 55〜63 (64 を超えないこと) */

static lv_obj_t *conn_label;
static lv_obj_t *caps_label;
static lv_obj_t *battery_label;
static lv_obj_t *name_label;
static lv_obj_t *layer_label;
static lv_obj_t *link_label; /* 右だけ: 左とつながっていないときの「L ✕」 */
static lv_obj_t *mod_labels[MOD_BOX_COUNT];
static lv_obj_t *tp_label;

static lv_color_t color_fg; /* 点灯する色 (テーマの文字の色) */
static lv_color_t color_bg; /* 背景の色 (テーマの画面の色) */

static bool screen_ready;
static bool rendered_once;
static struct status_state shown; /* 画面に出している状態 */

/* 白抜き (点灯した四角の上に背景の色の文字) と、ふつう (枠だけ) を切り替える */
static void set_inverted(lv_obj_t *obj, bool on, bool keep_border) {
    lv_obj_set_style_bg_color(obj, color_fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, on ? color_bg : color_fg, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, color_fg, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, keep_border ? 1 : 0, LV_PART_MAIN);
}

static void render_connection(const struct status_state *s) {
    char text[24];

#if STATUS_PERIPHERAL
    if (!s->link_connected) {
        lv_label_set_text(conn_label, "R " LV_SYMBOL_CLOSE);
        lv_label_set_text(name_label, "----");
        return;
    }
    if (!s->remote_valid) {
        lv_label_set_text(conn_label, "R " LV_SYMBOL_OK);
        lv_label_set_text(name_label, "...");
        return;
    }
#endif

    switch (s->transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), "%s",
                 s->transport_connected ? LV_SYMBOL_USB : LV_SYMBOL_USB " " LV_SYMBOL_CLOSE);
        lv_label_set_text(name_label, "USB");
        break;

    case ZMK_TRANSPORT_BLE: {
        const char *mark = !s->ble_bonded ? LV_SYMBOL_SETTINGS
                           : s->ble_connected ? LV_SYMBOL_OK
                                              : LV_SYMBOL_CLOSE;
        snprintf(text, sizeof(text), "BT%d %s", s->profile + 1, mark);
        if (s->profile < ARRAY_SIZE(PROFILE_NAMES)) {
            lv_label_set_text(name_label, PROFILE_NAMES[s->profile]);
        } else {
            lv_label_set_text_fmt(name_label, "BT%d", s->profile + 1);
        }
        break;
    }

    default:
        snprintf(text, sizeof(text), "%s", LV_SYMBOL_CLOSE);
        lv_label_set_text(name_label, "----");
        break;
    }

    lv_label_set_text(conn_label, text);
}

static void render_mods(const struct status_state *s) {
    static const char *const names_win[MOD_BOX_COUNT] = {"Sft", "Ctl", "Alt", "Win"};
    static const char *const names_mac[MOD_BOX_COUNT] = {"Sft", "Ctl", "Opt", "Cmd"};
    const uint8_t masks[MOD_BOX_COUNT] = {
        MOD_LSFT | MOD_RSFT,
        MOD_LCTL | MOD_RCTL,
        MOD_LALT | MOD_RALT,
        MOD_LGUI | MOD_RGUI,
    };
    const char *const *names = s->mac_style ? names_mac : names_win;

    for (int i = 0; i < MOD_BOX_COUNT; i++) {
        lv_label_set_text(mod_labels[i], names[i]);
        set_inverted(mod_labels[i], (s->mods & masks[i]) != 0, true);
    }
}

/* 状態を画面に出す。変わった所だけ書き直す (表示専用のスレッドで呼ばれる) */
static void render(const struct status_state *s) {
    if (!screen_ready) {
        return;
    }

    bool all = !rendered_once;

    if (all || s->transport != shown.transport ||
        s->transport_connected != shown.transport_connected || s->profile != shown.profile ||
        s->ble_connected != shown.ble_connected || s->ble_bonded != shown.ble_bonded ||
        s->link_connected != shown.link_connected || s->remote_valid != shown.remote_valid) {
        render_connection(s);
    }

    if (all || s->battery != shown.battery || s->usb_powered != shown.usb_powered) {
        lv_label_set_text_fmt(battery_label, "%s%u%%", s->usb_powered ? LV_SYMBOL_CHARGE : "",
                              (unsigned int)s->battery);
    }

    if (all || s->caps != shown.caps) {
        if (s->caps) {
            lv_obj_remove_flag(caps_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(caps_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (all || strcmp(s->layer_name, shown.layer_name) != 0) {
        lv_label_set_text(layer_label, s->layer_name);
    }

    if (link_label != NULL && (all || s->link_connected != shown.link_connected)) {
        if (s->link_connected) {
            lv_obj_add_flag(link_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(link_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (all || s->mods != shown.mods || s->mac_style != shown.mac_style) {
        render_mods(s);
    }

    if (all || s->tp_l != shown.tp_l || s->tp_r != shown.tp_r || s->fix_active != shown.fix_active ||
        strcmp(s->fix_text, shown.fix_text) != 0) {
        if (s->fix_active) {
            lv_label_set_text(tp_label, s->fix_text); /* TP 復旧のキーの結果 (10 秒) */
        } else if (s->tp_l == 0 || s->tp_r == 0) {
            lv_label_set_text(tp_label, "");
        } else {
            lv_label_set_text_fmt(tp_label, "TP L%3u%% R%3u%%", (unsigned int)s->tp_l,
                                  (unsigned int)s->tp_r);
        }
    }

    shown = *s;
    rendered_once = true;
}

/* ---------------------------------------------------------------- 右から左へ送る */

#if STATUS_SENDS_TO_PERIPHERAL

#define SYNC_DELAY_MS 10       /* 変わってから送るまで (続けて変わったときにまとめる) */
#define SYNC_RESEND_COUNT 3    /* つながったときに送り直す回数 */
#define SYNC_RESEND_FIRST_MS 2000
#define SYNC_RESEND_INTERVAL_MS 3000

static atomic_t sync_force;
static struct status_state sync_sent;
static bool sync_sent_valid;

static void sync_send(uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = OLEDSYNC_BEHAVIOR_NAME,
        .param1 = param1,
        .param2 = param2,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    for (uint8_t source = 0; source < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; source++) {
        int err = zmk_split_central_invoke_behavior(source, &binding, event, true);
        if (err < 0) {
            LOG_DBG("oledsync to %d failed: %d", source, err);
        }
    }
}

static void sync_send_state(const struct status_state *s) {
    uint32_t p1 = ((uint32_t)OLEDSYNC_OP_STATE << 24) | ((uint32_t)(s->transport & 0x3)) |
                  ((uint32_t)s->transport_connected << 2) | ((uint32_t)(s->profile & 0x7) << 3) |
                  ((uint32_t)s->ble_connected << 6) | ((uint32_t)s->ble_bonded << 7) |
                  ((uint32_t)s->mods << 8) | ((uint32_t)s->caps << 16) |
                  ((uint32_t)s->mac_style << 17);
    uint32_t p2 = (uint32_t)s->tp_l | ((uint32_t)s->tp_r << 16);

    sync_send(p1, p2);
}

static void sync_send_layer(const struct status_state *s) {
    const uint8_t *c = (const uint8_t *)s->layer_name; /* 8 バイト (後ろは 0 で埋まっている) */
    uint32_t p1 = ((uint32_t)OLEDSYNC_OP_LAYER << 24) | (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
                  ((uint32_t)c[2] << 16);
    uint32_t p2 = (uint32_t)c[3] | ((uint32_t)c[4] << 8) | ((uint32_t)c[5] << 16) |
                  ((uint32_t)c[6] << 24);

    sync_send(p1, p2);
}

/* 表示専用のスレッドで動かす (ZMK の送信の待ち行列が詰まって待たされても、キーの処理を止めないため) */
static void sync_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(sync_work, sync_work_cb);

static void sync_work_cb(struct k_work *work) {
    struct status_state s = gather_state(NULL);
    bool force = atomic_get(&sync_force) > 0;

    if (force || !sync_sent_valid || !shared_equal(&s, &sync_sent)) {
        sync_send_state(&s);
    }
    if (force || !sync_sent_valid || strcmp(s.layer_name, sync_sent.layer_name) != 0) {
        sync_send_layer(&s);
    }

    sync_sent = s;
    sync_sent_valid = true;

    if (force && atomic_dec(&sync_force) > 1) {
        k_work_reschedule_for_queue(zmk_display_work_q(), &sync_work,
                                    K_MSEC(SYNC_RESEND_INTERVAL_MS));
    }
}

static void sync_request(void) {
    k_work_reschedule_for_queue(zmk_display_work_q(), &sync_work, K_MSEC(SYNC_DELAY_MS));
}

/* BT がつながったら (左がつながり直したときも) 少し待って送り直す。どの相手かは区別しない */
static void sync_resend_soon(void) {
    atomic_set(&sync_force, SYNC_RESEND_COUNT);
    k_work_reschedule_for_queue(zmk_display_work_q(), &sync_work, K_MSEC(SYNC_RESEND_FIRST_MS));
}

/* 右が BT の central 側の接続 = 左との接続 (パソコンとの接続では右は peripheral 側) */
static bool is_link_to_left(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

/* 左とのつながりが変わったら画面を直す (ZMK のほかの処理と同じ system work queue で) */
static void link_refresh_work_cb(struct k_work *work) { status_refresh(NULL); }
K_WORK_DEFINE(link_refresh_work, link_refresh_work_cb);

static void status_bt_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0) {
        return;
    }
    if (is_link_to_left(conn)) {
        atomic_inc(&left_links);
        k_work_submit(&link_refresh_work);
    }
    if (zmk_display_is_initialized()) {
        sync_resend_soon();
    }
}

static void status_bt_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (is_link_to_left(conn)) {
        if (atomic_dec(&left_links) <= 0) {
            atomic_set(&left_links, 0);
        }
        k_work_submit(&link_refresh_work);
    }
}

BT_CONN_CB_DEFINE(split54tp_status_conn_cb) = {
    .connected = status_bt_connected,
    .disconnected = status_bt_disconnected,
};

#endif /* STATUS_SENDS_TO_PERIPHERAL */

/* ---------------------------------------------------------------- 左: 右から届いた内容 */

#if STATUS_PERIPHERAL
void split54tp_status_apply_remote(uint32_t param1, uint32_t param2) {
    k_mutex_lock(&remote_mutex, K_FOREVER);
    switch (OLEDSYNC_OP(param1)) {
    case OLEDSYNC_OP_STATE:
        remote.transport = param1 & 0x3;
        remote.transport_connected = (param1 >> 2) & 0x1;
        remote.profile = (param1 >> 3) & 0x7;
        remote.ble_connected = (param1 >> 6) & 0x1;
        remote.ble_bonded = (param1 >> 7) & 0x1;
        remote.mods = (param1 >> 8) & 0xFF;
        remote.caps = (param1 >> 16) & 0x1;
        remote.mac_style = (param1 >> 17) & 0x1;
        remote.tp_l = param2 & 0xFFFF;
        remote.tp_r = (param2 >> 16) & 0xFFFF;
        remote_valid = true;
        break;

    case OLEDSYNC_OP_LAYER:
        remote.layer_name[0] = param1 & 0xFF;
        remote.layer_name[1] = (param1 >> 8) & 0xFF;
        remote.layer_name[2] = (param1 >> 16) & 0xFF;
        remote.layer_name[3] = param2 & 0xFF;
        remote.layer_name[4] = (param2 >> 8) & 0xFF;
        remote.layer_name[5] = (param2 >> 16) & 0xFF;
        remote.layer_name[6] = (param2 >> 24) & 0xFF;
        remote.layer_name[OLEDSYNC_LAYER_NAME_LEN] = '\0';
        break;

    default:
        k_mutex_unlock(&remote_mutex);
        return;
    }
    k_mutex_unlock(&remote_mutex);

    status_refresh(NULL);
}
#endif /* STATUS_PERIPHERAL */

/* ---------------------------------------------------------------- イベント */

K_MUTEX_DEFINE(pending_mutex);
static struct status_state pending;
static bool pending_valid;

static void render_work_cb(struct k_work *work) {
    k_mutex_lock(&pending_mutex, K_FOREVER);
    struct status_state s = pending;
    k_mutex_unlock(&pending_mutex);

    render(&s);
}

K_WORK_DEFINE(render_work, render_work_cb);

static void status_refresh(const zmk_event_t *eh) {
    if (!zmk_display_is_initialized()) {
        return;
    }

    struct status_state s = gather_state(eh);

    k_mutex_lock(&pending_mutex, K_FOREVER);
    bool changed = !pending_valid || !state_equal(&s, &pending);
    pending = s;
    pending_valid = true;
    k_mutex_unlock(&pending_mutex);

    /* 何も変わっていなければ書き直さない (ふつうの文字のキーでは I2C を使わない) */
    if (changed) {
        k_work_submit_to_queue(zmk_display_work_q(), &render_work);
#if STATUS_SENDS_TO_PERIPHERAL
        sync_request();
#endif
    }
}

static int split54tp_status_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_SPLIT54TP_TP_FIX)
    const struct split54tp_tp_fix_done *fev = as_split54tp_tp_fix_done(eh);
    if (fev != NULL) {
        fix_store(fev);
    }
#endif

#if STATUS_PERIPHERAL
    /* 右と切れたら、届いていた内容は古いので使わない (つながり直すと右が送り直す) */
    const struct zmk_split_peripheral_status_changed *pev = as_zmk_split_peripheral_status_changed(eh);
    if (pev != NULL && !pev->connected) {
        k_mutex_lock(&remote_mutex, K_FOREVER);
        remote_valid = false;
        k_mutex_unlock(&remote_mutex);
    }
#endif

    status_refresh(eh);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split54tp_status, split54tp_status_listener);
#if STATUS_CENTRAL
ZMK_SUBSCRIPTION(split54tp_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(split54tp_status, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(split54tp_status, zmk_keycode_state_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(split54tp_status, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(split54tp_status, zmk_hid_indicators_changed);
#endif
#if TP_SPEED_SHOWN
ZMK_SUBSCRIPTION(split54tp_status, split54tp_tp_speed_changed);
#endif
#endif /* STATUS_CENTRAL */
#if STATUS_PERIPHERAL
ZMK_SUBSCRIPTION(split54tp_status, zmk_split_peripheral_status_changed);
#endif
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(split54tp_status, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(split54tp_status, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_SPLIT54TP_TP_FIX)
ZMK_SUBSCRIPTION(split54tp_status, split54tp_tp_fix_done);
#endif

/* ---------------------------------------------------------------- 画面を作る */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "");
    return label;
}

/* ZMK 本体の display/main.c が、表示の初期化のときに (表示専用のスレッドで) 1 回だけ呼ぶ */
lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* はみ出してもスクロールさせない (白黒テーマのスクロールバーは右端の太い白線になる) */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    /* テーマ (白黒) の色をそのまま使う。ZMK_DISPLAY_INVERT で反転しても合うように */
    color_fg = lv_obj_get_style_text_color(screen, LV_PART_MAIN);
    color_bg = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);

    /* 1 行目 */
    conn_label = make_label(screen, &lv_font_montserrat_12);
    lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 0, 0);

    battery_label = make_label(screen, &lv_font_montserrat_12);
    lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    caps_label = make_label(screen, &lv_font_unscii_8);
    lv_label_set_text(caps_label, "CAPS");
    lv_obj_set_style_pad_left(caps_label, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_right(caps_label, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_top(caps_label, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(caps_label, 1, LV_PART_MAIN);
    set_inverted(caps_label, true, false);
    lv_obj_align(caps_label, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_add_flag(caps_label, LV_OBJ_FLAG_HIDDEN);

    /* 接続先 (大きく) とレイヤー名 */
    name_label = make_label(screen, &lv_font_montserrat_26);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 13);

    layer_label = make_label(screen, &lv_font_montserrat_12);
    lv_obj_align(layer_label, LV_ALIGN_TOP_RIGHT, 0, 26);

#if STATUS_SENDS_TO_PERIPHERAL
    /* 左とつながっていないときだけ出す (レイヤー名の上。文字は y 15〜23 あたり) */
    link_label = make_label(screen, &lv_font_montserrat_12);
    lv_label_set_text(link_label, "L " LV_SYMBOL_CLOSE);
    lv_obj_align(link_label, LV_ALIGN_TOP_RIGHT, 0, 12);
    lv_obj_add_flag(link_label, LV_OBJ_FLAG_HIDDEN);
#endif

    /* 修飾キー 4 つ */
    for (int i = 0; i < MOD_BOX_COUNT; i++) {
        lv_obj_t *box = make_label(screen, &lv_font_unscii_8);
        lv_obj_set_size(box, MOD_BOX_W, MOD_BOX_H);
        lv_obj_set_style_text_align(box, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_pad_top(box, 1, LV_PART_MAIN);
        lv_obj_set_pos(box, i * (MOD_BOX_W + MOD_BOX_GAP), MOD_BOX_Y);
        set_inverted(box, false, true);
        mod_labels[i] = box;
    }

    /* TP の感度 */
    tp_label = make_label(screen, &lv_font_unscii_8);
    lv_obj_set_pos(tp_label, 0, TP_Y);

    screen_ready = true;

    struct status_state s = gather_state(NULL);
    k_mutex_lock(&pending_mutex, K_FOREVER);
    pending = s;
    pending_valid = true;
    k_mutex_unlock(&pending_mutex);
    render(&s);

#if STATUS_SENDS_TO_PERIPHERAL
    /* 起動のときは、左がもうつながっているかもしれないので送っておく */
    sync_resend_soon();
#endif

    return screen;
}
