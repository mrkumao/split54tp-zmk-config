# split54tp 用の自作機能 (2026-11-04)
#   zephyr/module.yml の cmake: . でここが読まれる。
#   GitHub Actions では zephyr/module.yml があると -DZMK_EXTRA_MODULES にこのリポジトリが渡される
#   (zmkfirmware/zmk の .github/workflows/build-user-config.yml)。

# 押した側の LPPS を校正する behavior。ペリフェラル (左) でも実行されるので左右ともコンパイルする
target_sources_ifdef(CONFIG_SPLIT54TP_TP_CALIB_LOCAL app PRIVATE src/behaviors/behavior_tp_calib_local.c)

# 感度の変更はキーマップと左右のスティックの受信があるセントラル (右) だけ
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources_ifdef(CONFIG_SPLIT54TP_TP_SPEED app PRIVATE
        src/pointing/input_processor_tp_speed.c
        src/behaviors/behavior_tp_speed.c
    )
endif()
