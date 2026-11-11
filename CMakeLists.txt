# split54tp 用の自作機能 (2026-11-04)
#   zephyr/module.yml の cmake: . でここが読まれる。
#   GitHub Actions では zephyr/module.yml があると -DZMK_EXTRA_MODULES にこのリポジトリが渡される
#   (zmkfirmware/zmk の .github/workflows/build-user-config.yml)。

# 押した側の LPPS を校正する behavior。ペリフェラル (左) でも実行されるので左右ともコンパイルする
target_sources_ifdef(CONFIG_SPLIT54TP_TP_CALIB_LOCAL app PRIVATE src/behaviors/behavior_tp_calib_local.c)

# ★ 2026-11-11 TP 復旧のキー (試し)。押した側の LPPS を読み直す。これも左右とも
target_sources_ifdef(CONFIG_SPLIT54TP_TP_FIX app PRIVATE src/behaviors/behavior_tp_fix.c)
# ★ 2026-11-11 ZMK が「操作」と数えるもの (キー・ホイール・入力) の数。TP 復旧のキーで OLED に出す (確認用)
target_sources_ifdef(CONFIG_SPLIT54TP_TP_FIX app PRIVATE src/display/activity_count.c)

# ★ 2026-11-11 LPPS の読み直し (TP 復旧のキーが使う) と、止まったときの自動の読み直し。LPPS がある半身で左右とも
target_sources_ifdef(CONFIG_SPLIT54TP_LPPS_RESCUE app PRIVATE src/pointing/lpps_rescue.c)

# 自作の宣言 (include/split54tp/)。★ 2026-11-09 左の画面でも使うので左右とも
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# 感度の変更はキーマップと左右のスティックの受信があるセントラル (右) だけ
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources_ifdef(CONFIG_SPLIT54TP_TP_SPEED app PRIVATE
        src/pointing/input_processor_tp_speed.c
        src/behaviors/behavior_tp_speed.c
    )
endif()

# ★ 2026-11-08 OLED の自作の画面 (接続先・レイヤー・修飾キー・Caps Lock・TP の感度)。
#   左右の .conf の CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y のときだけ。
# ★ 2026-11-09 左にも出す。右は自分で状態を集めて左へ送り、左は受け口 (behavior_oled_sync.c) で受ける
if (CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
    target_sources(app PRIVATE src/display/status_screen.c)
    if (CONFIG_ZMK_SPLIT AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        target_sources(app PRIVATE src/behaviors/behavior_oled_sync.c)
    endif()
endif()
