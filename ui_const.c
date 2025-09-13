#include "ui_const.h"
#include "LCD.h"
#include "RPN.h"
#include "key.h"

static bool g_const_ui_active = false;
static int g_const_group = 0; // 1 または 2
static int g_const_sel = 0;   // 0..9

static void render_const_ui(void)
{
    char line1[17];
    for (int i = 0; i < 16; ++i)
        line1[i] = ' ';
    const char *sym = rpn_const_symbol(g_const_group, g_const_sel);
    // 先頭に「n.」を置き、その後にシンボルを詰める
    char numch = (g_const_sel == 9) ? '0' : ('1' + g_const_sel);
    line1[0] = numch;
    line1[1] = '.';
    if (sym)
    {
        for (int i = 0; i < 13 && sym[i]; ++i)
        {
            line1[2 + i] = sym[i];
        }
    }
    line1[15] = key_get_shift_state() ? LCD_CHAR_UP_ARROW : LCD_CHAR_DOWN_ARROW;
    lcd_set_cursor(0, 0);
    lcd_write(line1, 16);

    const char *name = rpn_const_name(g_const_group, g_const_sel);
    if (!name)
        name = (g_const_group == 1) ? "Sci Const G1" : "Sci Const G2";
    lcd_write_line(1, name);
}

void const_ui_open(int group)
{
    g_const_group = group;
    g_const_sel = 0;
    g_const_ui_active = true;
    render_const_ui();
}

bool const_ui_is_open(void)
{
    return g_const_ui_active;
}

bool const_ui_handle_key(key_event_t ev)
{
    if (!g_const_ui_active)
        return false;
    if (ev.type == KEY_EVENT_UP || ev.type == KEY_EVENT_NONE)
        return false;
    switch (ev.code)
    {
    // UI外の操作に影響するキーは無効化
    case K_MODE:
    case K_P1:
    case K_P2:
    case K_P3:
    case K_PR:
        return false;
    case K_1:
        g_const_sel = 0;
        render_const_ui();
        return false;
    case K_2:
        g_const_sel = 1;
        render_const_ui();
        return false;
    case K_3:
        g_const_sel = 2;
        render_const_ui();
        return false;
    case K_4:
        g_const_sel = 3;
        render_const_ui();
        return false;
    case K_5:
        g_const_sel = 4;
        render_const_ui();
        return false;
    case K_6:
        g_const_sel = 5;
        render_const_ui();
        return false;
    case K_7:
        g_const_sel = 6;
        render_const_ui();
        return false;
    case K_8:
        g_const_sel = 7;
        render_const_ui();
        return false;
    case K_9:
        g_const_sel = 8;
        render_const_ui();
        return false;
    case K_0:
        g_const_sel = 9;
        render_const_ui();
        return false;
    case K_ROLL:
        g_const_sel = (g_const_sel + 1) % 10;
        render_const_ui();
        return false;
    case K_ROLLUP:
        g_const_sel = (g_const_sel == 0) ? 9 : (g_const_sel - 1);
        render_const_ui();
        return false;
    case K_SHIFT:
        render_const_ui();
        return false;
    case K_C1:
        g_const_group = 1;
        render_const_ui();
        return false;
    case K_C2:
        g_const_group = 2;
        render_const_ui();
        return false;
    case K_ENTER:
        rpn_const_apply(g_const_group, g_const_sel);
        key_set_shift_state(false);
        g_const_ui_active = false;
        return true;
    case K_DEL:
    case K_OFF:
        key_set_shift_state(false);
        g_const_ui_active = false;
        return true;
    default:
        return false;
    }
}
