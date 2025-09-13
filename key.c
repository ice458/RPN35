#include "key.h"
#include "pico/stdlib.h"
#include <string.h>
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware_definition.h"
#include "clock_ctrl.h"

// 内部状態
typedef struct
{
    uint8_t row_sel;             // 0..6
    uint8_t stable_code;         // 前回確定キー（0=なし）
    uint8_t last_code;           // 直近スキャンの生コード
    uint8_t debounce;            // デバウンスカウンタ
    uint16_t repeat_ms;          // リピート間隔管理
    repeating_timer_t timer;     // スキャンタイマ
    bool shift_state;            // シフト状態（トグル）
    key_event_t events[8];       // イベントバッファ（リングバッファ）
    volatile uint8_t event_head; // リングバッファヘッド
    volatile uint8_t event_tail; // リングバッファテイル
    bool timer_running;          // スキャンタイマ稼働状態
} key_state_t;

static key_state_t gk;

// リングバッファ操作関数
static bool enqueue_event(key_event_t event)
{
    uint8_t next_head = (gk.event_head + 1) % 8;
    if (next_head == gk.event_tail)
    {
        // バッファフル - 最古のイベントを削除
        gk.event_tail = (gk.event_tail + 1) % 8;
    }
    gk.events[gk.event_head] = event;
    gk.event_head = next_head;
    return true;
}

static key_event_t dequeue_event(void)
{
    if (gk.event_head == gk.event_tail)
    {
        return (key_event_t){KEY_EVENT_NONE, K_NONE};
    }
    key_event_t event = gk.events[gk.event_tail];
    gk.event_tail = (gk.event_tail + 1) % 8;
    return event;
}

// 生コード(行×列)から抽象キーへのマッピング
static key_code_t map_raw_to_key(uint8_t raw)
{
    // 列ビットは下位5bit、行は5bit目以降。
    uint8_t col_mask = raw & 0x1F; // 下位5bit
    uint8_t row = (raw >> 5) & 0x07;
    // 列のビット位置(25..29)を 0..4 に正規化済みとみなす。
    int col = -1;
    for (int i = 0; i < 5; i++)
    {
        if (col_mask & (1u << i))
        {
            col = i;
            break;
        }
    }
    if (col < 0)
        return K_NONE;

    // キーマトリクスの論理配置に応じた写像
    switch (row)
    {
    case 0: // ROW1
        switch (col)
        {
        case 0:
            return K_LOGXY;
        case 1:
            return gk.shift_state ? K_DISP : K_C2;
        case 2:
            return gk.shift_state ? K_MODE : K_C1;
        case 3:
            return gk.shift_state ? K_PR : K_P2;
        case 4:
            return gk.shift_state ? K_P3 : K_P1;
        }
        break;
    case 1: // ROW2
        switch (col)
        {
        case 0:
            return gk.shift_state ? K_EXP : K_LN;
        case 1:
            return gk.shift_state ? K_POW10 : K_LOG;
        case 2:
            return gk.shift_state ? K_NTH_ROOT : K_POW;
        case 3:
            return gk.shift_state ? K_POW3 : K_POW2;
        case 4:
            return gk.shift_state ? K_CUBE_ROOT : K_SQRT;
        }

        break;
    case 2: // ROW3
        switch (col)
        {
        case 0:
            return gk.shift_state ? K_ATAN : K_TAN;
        case 1:
            return gk.shift_state ? K_ACOS : K_COS;
        case 2:
            return gk.shift_state ? K_ASIN : K_SIN;
        case 3:
            return gk.shift_state ? K_LAST : K_SWAP;
        case 4:
            return gk.shift_state ? K_ROLLUP : K_ROLL;
        }
        break;
    case 3: // ROW4
        switch (col)
        {
        case 0:
            return gk.shift_state ? K_OFF : K_DEL;
        case 1:
            return K_SIGN;
        case 2:
            return gk.shift_state ? K_VF : K_9;
        case 3:
            return gk.shift_state ? K_VE : K_8;
        case 4:
            return gk.shift_state ? K_VD : K_7;
        }
        break;
    case 4: // ROW5
        switch (col)
        {
        case 0:
            return gk.shift_state ? K_REV : K_DIV;
        case 1:
            return gk.shift_state ? K_FACT : K_MUL;
        case 2:
            return gk.shift_state ? K_VC : K_6;
        case 3:
            return gk.shift_state ? K_VB : K_5;
        case 4:
            return gk.shift_state ? K_VA : K_4;
        }
        break;
    case 5: // ROW6
        switch (col)
        {
        case 0:
            return K_SUB;
        case 1:
            return K_ADD;
        case 2:
            return gk.shift_state ? K_CLR : K_3;
        case 3:
            return gk.shift_state ? K_ST : K_2;
        case 4:
            return gk.shift_state ? K_LD : K_1;
        }
        break;
    case 6: // ROW7
        switch (col)
        {
        case 0:
            return K_ENTER;
        case 1:
            return K_SHIFT;
        case 2:
            return gk.shift_state ? K_PI : K_EE;
        case 3:
            return gk.shift_state ? K_e : K_DOT;
        case 4:
            return gk.shift_state ? K_SHOW : K_0;
        }
        break;
    }
    return K_NONE;
}

static void drive_row(uint8_t row, bool high)
{
    uint pin = 0;
    switch (row)
    {
    case 0:
        pin = ROW1;
        break;
    case 1:
        pin = ROW2;
        break;
    case 2:
        pin = ROW3;
        break;
    case 3:
        pin = ROW4;
        break;
    case 4:
        pin = ROW5;
        break;
    case 5:
        pin = ROW6;
        break;
    case 6:
        pin = ROW7;
        break;
    default:
        return;
    }
    gpio_put(pin, high ? 1 : 0);
}

static bool scan_timer_cb(repeating_timer_t *rt)
{
    // 1回のタイマー呼び出しで全行を走査し、最初に検出したキーをrawとして採用
    uint8_t raw = 0;
    for (uint8_t row = 0; row < 7; ++row)
    {
        drive_row(row, true);
        // セトリング時間を増加（クロストーク対策）
        for (volatile int i = 0; i < 100; ++i)
            __asm volatile("nop");
        uint8_t cols = (uint8_t)((gpio_get_all() & COL_MASK) >> 25u);
        drive_row(row, false);
        // 行間の微小遅延（クロストーク対策）
        for (volatile int i = 0; i < 100; ++i)
            __asm volatile("nop");

        if (cols)
        {
            raw = (uint8_t)(cols | (row << 5));
            break; // 最初の押下のみ扱う（複数同時押しは非対応）
        }
    }

    // デバウンスとイベント生成
    if (raw != gk.last_code)
    {
        // 何かしらのキーが押下されたタイミングで即時クロック高速化
        clockctrl_boost_now();
        gk.debounce = 0;
        gk.last_code = raw;
    }
    else
    {
        if (gk.debounce < 3) // デバウンス期間を3回連続読み取り
            gk.debounce++;
        if (gk.debounce == 3)
        {
            // 安定化
            if (gk.stable_code != raw)
            {
                // 状態遷移: up or down
                key_event_t ev = {0};
                if (raw)
                {
                    ev.type = KEY_EVENT_DOWN;
                    key_code_t key = map_raw_to_key(raw);

                    // シフトキーの場合はトグル処理
                    if (key == K_SHIFT)
                    {
                        gk.shift_state = !gk.shift_state;
                    }
                    else
                    {
                        // シフトキー以外の場合、非シフト状態のキーとして処理
                        // （シフト状態は次のキーまで維持）
                    }

                    ev.code = key;
                    gk.repeat_ms = 0;
                }
                else
                {
                    ev.type = KEY_EVENT_UP;
                    ev.code = map_raw_to_key(gk.stable_code);
                }
                gk.stable_code = raw;
                enqueue_event(ev); // リングバッファに追加
            }
            else if (raw)
            {
                // キー押しっぱなし（リピート）
                gk.repeat_ms += (uint16_t)50 / TIMER_TICK; // 周期5ms相当
                if (gk.repeat_ms >= (uint16_t)2500 / TIMER_TICK)
                {                                               // 500ms後から100ms間隔
                    gk.repeat_ms = (uint16_t)2000 / TIMER_TICK; // 次は100msで発火（500-100=400）
                    key_event_t ev = {KEY_EVENT_REPEAT, map_raw_to_key(raw)};
                    enqueue_event(ev);
                }
            }
        }
    }

    return true;
}

void key_init(void)
{
    memset(&gk, 0, sizeof(gk));
    // リングバッファ初期化
    gk.event_head = gk.event_tail = 0;

    // 行: 出力 Low 初期化
    gpio_init_mask(ROW_MASK);
    gpio_set_dir_out_masked(ROW_MASK);
    gpio_put_masked(ROW_MASK, 0);

    // 列: 入力 + プルダウン
    gpio_init_mask(COL_MASK);
    gpio_set_dir_in_masked(COL_MASK);
    gpio_pull_down(COL1);
    gpio_pull_down(COL2);
    gpio_pull_down(COL3);
    gpio_pull_down(COL4);
    gpio_pull_down(COL5);

    // スキャン開始
    add_repeating_timer_ms(-1 * TIMER_TICK, &scan_timer_cb, NULL, &gk.timer);
    gk.timer_running = true;
}

key_event_t key_poll(void)
{
    uint32_t irq = save_and_disable_interrupts();
    key_event_t ev = dequeue_event();
    restore_interrupts(irq);
    return ev;
}

void key_reset(void)
{
    uint32_t irq = save_and_disable_interrupts();
    gk.stable_code = gk.last_code = 0;
    gk.debounce = 0;
    gk.shift_state = false;
    // リングバッファをクリア
    gk.event_head = gk.event_tail = 0;
    restore_interrupts(irq);
}

void key_scan_pause(void)
{
    uint32_t irq = save_and_disable_interrupts();
    if (gk.timer_running)
    {
        cancel_repeating_timer(&gk.timer);
        gk.timer_running = false;
    }
    restore_interrupts(irq);
}

void key_scan_resume(void)
{
    uint32_t irq = save_and_disable_interrupts();
    if (!gk.timer_running)
    {
        add_repeating_timer_ms(-1 * TIMER_TICK, &scan_timer_cb, NULL, &gk.timer);
        gk.timer_running = true;
    }
    restore_interrupts(irq);
}

void key_set_shift_state(bool shift_on)
{
    uint32_t irq = save_and_disable_interrupts();
    gk.shift_state = shift_on;
    restore_interrupts(irq);
}

bool key_get_shift_state(void)
{
    return gk.shift_state;
}
