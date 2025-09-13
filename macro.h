#ifndef MACRO_H
#define MACRO_H

#include <stdbool.h>
#include "key.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // 初期化
    void macro_init(void);

    // 記録中か
    bool macro_is_recording(void);
    // 再生中か
    bool macro_is_playing(void);

    // 記録開始（slot: 0=P1, 1=P2, 2=P3）既存は消去して上書き
    void macro_start_record(int slot);
    // 記録停止
    void macro_stop_record(void);
    // スロットにデータがあるか
    bool macro_has(int slot);

    // 再生開始（存在しなければfalse）
    bool macro_play(int slot);
    // 再生中断（ユーザー操作割り込み等）
    void macro_cancel_play(void);

    // 記録用フック：この関数でイベントを記録する（通常はKEY_EVENT_DOWNのみ記録）
    void macro_capture_event(key_event_t ev);

    // 再生用フック：キューに注入すべきイベントがあればtrueを返し、out_evに設定
    bool macro_inject_next(key_event_t *out_ev);

    // 変更があればフラッシュに保存（電源OFF直前などで呼ぶ）
    void macro_save_if_dirty(void);

    // すべてのマクロスロットを消去し、フラッシュへ即保存
    void macro_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif // MACRO_H
