#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "RPN.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // 自動電源OFFの設定
    typedef enum
    {
        AUTO_OFF_DISABLED = 0,
        AUTO_OFF_3_MIN,
        AUTO_OFF_5_MIN,
        AUTO_OFF_10_MIN,
        AUTO_OFF__COUNT
    } auto_off_mode_t;

    // 初期化（フラッシュから読み出し）
    void settings_init(void);
    // フラッシュからロードした値、または未設定時のデフォルトを out に書き込む
    void settings_load_into(init_state_t *out);
    // RPNのsetterが呼ばれたら最新値を通知（変更検出しdirty管理）
    void settings_on_values_changed(disp_mode_t disp, angle_mode_t angle, hyperbolic_mode_t hyperb, zero_mode_t zero);
    // 電源OFF直前に呼ぶ。電源投入以降に変更がある場合のみ保存する
    void settings_save_if_dirty(void);

    // フラッシュの保存内容をデフォルトにリセットし、即時書き込みする
    void settings_reset_to_defaults(void);

    // 自動電源OFF設定の取得/設定
    auto_off_mode_t settings_get_auto_off_mode(void);
    void settings_set_auto_off_mode(auto_off_mode_t mode);
    // ms に変換（0=オートOFF無し）
    uint32_t settings_get_auto_off_ms(void);

    // LCD コントラスト（25-50 に制限）
    uint8_t settings_get_lcd_contrast(void);
    void settings_set_lcd_contrast(uint8_t value); // 範囲外は丸め

#ifdef __cplusplus
}
#endif

#endif
