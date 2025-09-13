#include "ui_macro.h"
#include "LCD.h"
#include "macro.h"
#include "key.h"
#include "pico/stdlib.h"

static bool g_macro_ui_active = false;
static int g_macro_rec_slot = 0; // 0..2

// 上書き確認ダイアログ（ENTER=Yes, DEL/OFF=No）
static bool confirm_overwrite_dialog(int slot)
{
    // シフトは解除
    key_set_shift_state(false);
    lcd_clear();
    // 1行目: Overwrite Pn ?
    char line1[17];
    for (int i = 0; i < 16; ++i)
        line1[i] = ' ';
    const char *prefix = "Overwrite ";
    int p = 0;
    while (p < 16 && prefix[p])
    {
        line1[p] = prefix[p];
        p++;
    }
    if (p < 15)
    {
        line1[p++] = 'P';
        if (p < 16)
            line1[p++] = (char)('1' + (slot & 3));
    }
    if (p < 16)
        line1[p++] = ' ';
    if (p < 16)
        line1[p++] = '?';
    lcd_set_cursor(0, 0);
    lcd_write(line1, 16);

    // 2行目: ENTER=Yes DEL=No
    char line2[17];
    for (int i = 0; i < 16; ++i)
        line2[i] = ' ';
    const char *msg = "ENTER=Yes DEL=No";
    for (int i = 0; i < 16 && msg[i]; ++i)
        line2[i] = msg[i];
    lcd_set_cursor(1, 0);
    lcd_write(line2, 16);

    // 入力待ち
    while (1)
    {
        key_event_t ev = key_poll();
        if (ev.type == KEY_EVENT_DOWN)
        {
            if (ev.code == K_ENTER)
                return true; // 上書き実行
            if (ev.code == K_DEL || ev.code == K_OFF)
                return false; // キャンセル
        }
        sleep_ms(10);
    }
}

static void render_macro_ui(void)
{
    char line1[17];
    for (int i = 0; i < 16; ++i)
        line1[i] = ' ';
    const char *title = "Push PR to Rec.";
    for (int i = 0; i < 16 && title[i]; ++i)
        line1[i] = title[i];
    line1[15] = key_get_shift_state() ? 's' : ' ';
    lcd_set_cursor(0, 0);
    lcd_write(line1, 16);

    char line2[17];
    for (int i = 0; i < 16; ++i)
        line2[i] = ' ';
    const int box_positions[3] = {0, 5, 10};
    for (int i = 0; i < 3; ++i)
    {
        int pos = box_positions[i];
        bool sel = (i == g_macro_rec_slot);
        if (sel)
        {
            line2[pos + 0] = '[';
            line2[pos + 1] = 'P';
            line2[pos + 2] = '1' + i;
            line2[pos + 3] = ']';
        }
        else
        {
            line2[pos + 0] = ' ';
            line2[pos + 1] = 'P';
            line2[pos + 2] = '1' + i;
            line2[pos + 3] = ' ';
        }
    }
    lcd_set_cursor(1, 0);
    lcd_write(line2, 16);
}

void macro_ui_open(void)
{
    g_macro_ui_active = true;
    render_macro_ui();
}

bool macro_ui_is_open(void)
{
    return g_macro_ui_active;
}

bool macro_ui_handle_key(key_event_t ev)
{
    if (!g_macro_ui_active)
        return false;
    if (ev.type == KEY_EVENT_UP || ev.type == KEY_EVENT_NONE)
        return false;
    switch (ev.code)
    {
    case K_P1:
        g_macro_rec_slot = 0;
        render_macro_ui();
        return false;
    case K_P2:
        g_macro_rec_slot = 1;
        render_macro_ui();
        return false;
    case K_P3:
        g_macro_rec_slot = 2;
        render_macro_ui();
        return false;
    case K_SHIFT:
        render_macro_ui();
        return false;
    case K_PR:
        if (!macro_is_recording())
        {
            // 既存がある場合は上書き確認
            if (macro_has(g_macro_rec_slot))
            {
                bool ok = confirm_overwrite_dialog(g_macro_rec_slot);
                if (!ok)
                {
                    // キャンセル: UI再描画して継続
                    render_macro_ui();
                    return false;
                }
            }
            macro_start_record(g_macro_rec_slot);
            key_set_shift_state(false);
            g_macro_ui_active = false;
            return true; // 通常画面へ復帰
        }
        else
        {
            macro_stop_record();
            render_macro_ui();
            return false;
        }
    case K_DEL:
    case K_CLR:
        key_set_shift_state(false);
        g_macro_ui_active = false;
        return true;
    default:
        return false;
    }
}
