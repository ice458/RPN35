#ifndef KEY_H
#define KEY_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>

#define TIMER_TICK 10

    // 押下/離上のイベント種別
    typedef enum
    {
        KEY_EVENT_NONE = 0,
        KEY_EVENT_DOWN,
        KEY_EVENT_UP,
        KEY_EVENT_REPEAT
    } key_event_type_t;

    // 論理キー（行列配線から独立した抽象名）
    typedef enum
    {
        K_NONE = 0,
        K_0,
        K_1,
        K_2,
        K_3,
        K_4,
        K_5,
        K_6,
        K_7,
        K_8,
        K_9,
        K_DOT,
        K_EE,
        K_ENTER,
        K_SHIFT,
        K_DEL,
        K_ADD,
        K_SUB,
        K_MUL,
        K_DIV,
        K_SWAP,
        K_ROLL,
        K_SIN,
        K_COS,
        K_TAN,
        K_SQRT,
        K_POW2,
        K_POW,
        K_LOG,
        K_LN,
        K_P1,
        K_P2,
        K_C1,
        K_C2,
        K_LOGXY,
        K_SIGN,
        K_ASIN,
        K_ACOS,
        K_ATAN,
        K_EXP,
        K_POW10,
        K_FACT,
        K_ROLLUP,
        K_REV,
        K_OFF,
        K_LAST,
        K_CUBE_ROOT,
        K_POW3,
        K_NTH_ROOT,
        K_P3,
        K_PR,
        K_MODE,
        K_DISP,
        K_ST,
        K_LD,
        K_CLR,
        K_VA,
        K_VB,
        K_VC,
        K_VD,
        K_VE,
        K_VF,
        K_SHOW,
        K_PI,
        K_e,
    } key_code_t;

    typedef struct
    {
        key_event_type_t type;
        key_code_t code;
    } key_event_t;

    // シフト状態の設定/取得
    void key_set_shift_state(bool shift_on);
    bool key_get_shift_state(void);
    // 初期化
    void key_init(void);
    // 未処理のイベントがあれば1件取得（なければ type=NONE）
    key_event_t key_poll(void);
    // 全状態クリア
    void key_reset(void);

    // スキャンタイマを一時停止/再開（クロック切替前後の安全確保用）
    void key_scan_pause(void);
    void key_scan_resume(void);

#ifdef __cplusplus
}
#endif

#endif