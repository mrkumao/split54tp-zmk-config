/*
 * split54tp: 右 (セントラル) の OLED の自作の画面 (2026-11-08)
 *
 *   128 x 64 の配置 (y は上から。文字の高さは目安で、実機で詰める):
 *     y  0〜   1 行目 (Montserrat 12)  左 = BT の番号と接続 / 中 = CAPS (点灯中だけ白抜き) / 右 = 電池 %
 *     y 13〜   大きく (Montserrat 26)  接続先の名前 (Mac / Win / iPad / Spare / USB)。右に今のレイヤー名
 *     y 44〜54 修飾キー 4 つ (UNSCII 8) Sft / Ctl / Alt / Win (Mac・iPad のレイヤーのときは Opt / Cmd)。押している間だけ白抜き
 *     y 56〜63 TP の感度 (UNSCII 8)     左・右の %
 *
 *   ・接続先の名前は BT のプロファイル番号から決める (キーマップの prof1_mac など: 1 = Mac、2 = Win、3 = iPad、4 = 予備)。
 *     番号と名前の対応を変えたら PROFILE_NAMES も直すこと。
 *   ・Opt / Cmd にするかは「Mac」「iPad」という名前のレイヤーが有効かで決める (キーマップの display-name)。
 *   ・Caps Lock はパソコンから届くランプの状態 (CONFIG_ZMK_HID_INDICATORS)。選んでいる接続先の分を出す。
 *   ・修飾キーは左右どちらでも同じ枠が光る。
 *   ・I2C は LPPS と同じ線なので、表示が変わったときだけ書き直す (キーを押すたびには書かない)。
 *
 *   作り方は ZMK 標準の画面 (app/src/display/status_screen.c と widgets/) と同じ:
 *   ZMK のイベントで状態を集め、表示の作業は表示専用のスレッド (zmk_display_work_q) で行う。
 *   https://github.com/zmkfirmware/zmk/tree/main/app/src/display
 *   https://zmk.dev/docs/features/displays
 */

#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
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

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>
#endif

/* TP の感度 (src/pointing/input_processor_tp_speed.c)。split54tp.dtsi の tp_speed_l / tp_speed_r */
#define TP_SPEED_SHOWN                                                                             \
    (IS_ENABLED(CONFIG_SPLIT54TP_TP_SPEED) && DT_NODE_HAS_STATUS(DT_NODELABEL(tp_speed_l), okay) && \
     DT_NODE_HAS_STATUS(DT_NODELABEL(tp_speed_r), okay))

#if TP_SPEED_SHOWN
#include <split54tp/tp_speed.h>
static const struct device *const tp_dev_l = DEVICE_DT_GET(DT_NODELABEL(tp_speed_l));
static const struct device *const tp_dev_r = DEVICE_DT_GET(DT_NODELABEL(tp_speed_r));
#endif

/* BT のプロファイル番号 (0 から) → 大きく出す名前。キーマップの prof1_mac〜prof4_spare と合わせる */
static const char *const PROFILE_NAMES[] = {"Mac", "Win", "iPad", "Spare"};

/* Opt / Cmd の表記にするレイヤーの名前 (キーマップの display-name) */
static const char *const MAC_LAYER_NAMES[] = {"Mac", "iPad"};

/* Caps Lock のランプのビット (HID の LED のページ: ビット 0 = Num Lock、ビット 1 = Caps Lock) */
#define CAPS_LOCK_BIT BIT(HID_USAGE_LED_CAPS_LOCK - 1)

/* ---------------------------------------------------------------- 状態 */

struct status_state {
    enum zmk_transport transport; /* 出している (つながっていなければ、つなごうとしている) 出力先 */
    bool transport_connected;     /* 出力先がつながっている */
    uint8_t profile;              /* BT のプロファイル番号 (0 から) */
    bool ble_connected;
    bool ble_bonded; /* 登録済み (false = ペアリング待ち) */
    uint8_t battery;
    bool usb_powered;
    zmk_keymap_layer_index_t layer_index;
    const char *layer_name;
    bool mac_style;
    zmk_mod_flags_t mods;
    bool caps;
    uint16_t tp_l;
    uint16_t tp_r;
};

static bool state_equal(const struct status_state *a, const struct status_state *b) {
    return a->transport == b->transport && a->transport_connected == b->transport_connected &&
           a->profile == b->profile && a->ble_connected == b->ble_connected &&
           a->ble_bonded == b->ble_bonded && a->battery == b->battery &&
           a->usb_powered == b->usb_powered && a->layer_index == b->layer_index &&
           a->layer_name == b->layer_name && a->mac_style == b->mac_style && a->mods == b->mods &&
           a->caps == b->caps && a->tp_l == b->tp_l && a->tp_r == b->tp_r;
}

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

/*
 * 今の状態を集める (イベントの処理の中で呼ばれる)。
 * 修飾キーは、ZMK 本体が HID の状態を直すのと、この画面がイベントを受けるのと、どちらが先か決まっていないので、
 * 今の HID の状態に、受けたイベントの修飾キーの押す / 離すを重ねる (どちらの順でも同じ結果になる)。
 */
static struct status_state gather_state(const zmk_event_t *eh) {
    struct status_state s;
    memset(&s, 0, sizeof(s));

    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    s.transport_connected = selected.transport != ZMK_TRANSPORT_NONE;
    s.transport =
        s.transport_connected ? selected.transport : zmk_endpoint_get_preferred_transport();

#if IS_ENABLED(CONFIG_ZMK_BLE)
    s.profile = (uint8_t)zmk_ble_active_profile_index();
    s.ble_connected = zmk_ble_active_profile_is_connected();
    s.ble_bonded = !zmk_ble_active_profile_is_open();
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    const struct zmk_battery_state_changed *bev =
        (eh != NULL) ? as_zmk_battery_state_changed(eh) : NULL;
    s.battery = (bev != NULL) ? bev->state_of_charge : zmk_battery_state_of_charge();
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
    s.usb_powered = zmk_usb_is_powered();
#endif

    s.layer_index = zmk_keymap_highest_layer_active();
    s.layer_name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(s.layer_index));
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

    return s;
}

/* ---------------------------------------------------------------- 画面 */

#define MOD_BOX_COUNT 4
#define MOD_BOX_W 30
#define MOD_BOX_H 11 /* 枠 1 + 上の余白 1 + 文字 8 + 枠 1。y 44〜54 */
#define MOD_BOX_GAP 2

static lv_obj_t *conn_label;
static lv_obj_t *caps_label;
static lv_obj_t *battery_label;
static lv_obj_t *name_label;
static lv_obj_t *layer_label;
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
    const zmk_mod_flags_t masks[MOD_BOX_COUNT] = {
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
        s->ble_connected != shown.ble_connected || s->ble_bonded != shown.ble_bonded) {
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

    if (all || s->layer_index != shown.layer_index || s->layer_name != shown.layer_name) {
        if (s->layer_name != NULL && s->layer_name[0] != '\0') {
            lv_label_set_text(layer_label, s->layer_name);
        } else {
            lv_label_set_text_fmt(layer_label, "L%u", (unsigned int)s->layer_index);
        }
    }

    if (all || s->mods != shown.mods || s->mac_style != shown.mac_style) {
        render_mods(s);
    }

    if (all || s->tp_l != shown.tp_l || s->tp_r != shown.tp_r) {
#if TP_SPEED_SHOWN
        lv_label_set_text_fmt(tp_label, "TP L%3u%% R%3u%%", (unsigned int)s->tp_l,
                              (unsigned int)s->tp_r);
#else
        lv_label_set_text(tp_label, "");
#endif
    }

    shown = *s;
    rendered_once = true;
}

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

static int split54tp_status_listener(const zmk_event_t *eh) {
    if (!zmk_display_is_initialized()) {
        return ZMK_EV_EVENT_BUBBLE;
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
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split54tp_status, split54tp_status_listener);
ZMK_SUBSCRIPTION(split54tp_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(split54tp_status, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(split54tp_status, zmk_keycode_state_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(split54tp_status, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(split54tp_status, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(split54tp_status, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(split54tp_status, zmk_hid_indicators_changed);
#endif
#if TP_SPEED_SHOWN
ZMK_SUBSCRIPTION(split54tp_status, split54tp_tp_speed_changed);
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
    lv_obj_set_style_pad_top(caps_label, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(caps_label, 1, LV_PART_MAIN);
    set_inverted(caps_label, true, false);
    lv_obj_align(caps_label, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_add_flag(caps_label, LV_OBJ_FLAG_HIDDEN);

    /* 接続先 (大きく) とレイヤー名 */
    name_label = make_label(screen, &lv_font_montserrat_26);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 13);

    layer_label = make_label(screen, &lv_font_montserrat_12);
    lv_obj_align(layer_label, LV_ALIGN_TOP_RIGHT, 0, 24);

    /* 修飾キー 4 つ */
    for (int i = 0; i < MOD_BOX_COUNT; i++) {
        lv_obj_t *box = make_label(screen, &lv_font_unscii_8);
        lv_obj_set_size(box, MOD_BOX_W, MOD_BOX_H);
        lv_obj_set_style_text_align(box, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_pad_top(box, 1, LV_PART_MAIN);
        lv_obj_set_pos(box, i * (MOD_BOX_W + MOD_BOX_GAP), 44);
        set_inverted(box, false, true);
        mod_labels[i] = box;
    }

    /* TP の感度 */
    tp_label = make_label(screen, &lv_font_unscii_8);
    lv_obj_set_pos(tp_label, 0, 56);

    screen_ready = true;

    struct status_state s = gather_state(NULL);
    k_mutex_lock(&pending_mutex, K_FOREVER);
    pending = s;
    pending_valid = true;
    k_mutex_unlock(&pending_mutex);
    render(&s);

    return screen;
}
