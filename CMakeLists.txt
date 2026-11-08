# split54tp 用の自作機能 (2026-11-04)
#   zephyr/module.yml の cmake: . でここが読まれる。
#   GitHub Actions では zephyr/module.yml があると -DZMK_EXTRA_MODULES にこのリポジトリが渡される
#   (zmkfirmware/zmk の .github/workflows/build-user-config.yml)。

# 押した側の LPPS を校正する behavior。ペリフェラル (左) でも実行されるので左右ともコンパイルする
target_sources_ifdef(CONFIG_SPLIT54TP_TP_CALIB_LOCAL app PRIVATE src/behaviors/behavior_tp_calib_local.c)

# 感度の変更はキーマップと左右のスティックの受信があるセントラル (右) だけ
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    # ★ 2026-11-08 感度の値とイベントの宣言 (include/split54tp/tp_speed.h) を、感度のコードと画面から読む
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

    target_sources_ifdef(CONFIG_SPLIT54TP_TP_SPEED app PRIVATE
        src/pointing/input_processor_tp_speed.c
        src/behaviors/behavior_tp_speed.c
    )

    # ★ 2026-11-08 右の OLED の自作の画面 (接続先・レイヤー・修飾キー・Caps Lock・TP の感度)。
    #   split54tp_right.conf の CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y のときだけ。左は ZMK 標準の画面のまま
    if (CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
        target_sources(app PRIVATE src/display/status_screen.c)
    endif()
endif()
