#ifndef UI_MACRO_H
#define UI_MACRO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include "key.h"

    // マクロUIを開く（前回選択スロットを保持）
    void macro_ui_open(void);

    // マクロUIが開いているか
    bool macro_ui_is_open(void);

    // マクロUIへキーイベントを渡す。trueを返したときはメイン側で通常画面再描画。
    bool macro_ui_handle_key(key_event_t ev);

#ifdef __cplusplus
}
#endif

#endif // UI_MACRO_H
