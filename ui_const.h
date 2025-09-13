#ifndef UI_CONST_H
#define UI_CONST_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include "key.h"

    // UI起動（group は 1 または 2）。選択は 0 に初期化、即座に描画する。
    void const_ui_open(int group);

    // UIが開いているか
    bool const_ui_is_open(void);

    // キー処理（従来の handle_const_ui と同等の戻り値ポリシー）
    // true: 通常画面の再描画が必要（UIを閉じた等）
    // false: UI内で再描画済み
    bool const_ui_handle_key(key_event_t ev);

#ifdef __cplusplus
}
#endif

#endif // UI_CONST_H
