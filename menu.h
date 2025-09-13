#ifndef MENU_H
#define MENU_H

#include <stdbool.h>
#include "key.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // メニュー制御API（汎用/階層対応）
    void menu_init(void);                 // メニューシステム初期化
    void menu_open(void);                 // ルートメニューを開く
    void menu_close(void);                // メニューを閉じる
    bool menu_is_open(void);              // メニューが開いているか
    bool menu_handle_key(key_event_t ev); // キー処理（true=再描画必要）
    void menu_render(void);               // LCDへ現在メニューを描画
    bool menu_needs_redraw(void);         // 再描画が必要かチェック
    void menu_clear_redraw_flag(void);    // 再描画フラグをクリア

#ifdef __cplusplus
}
#endif

#endif
