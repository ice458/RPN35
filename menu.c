#include "menu.h"
#include <string.h>
#include <stdio.h>
#include "LCD.h"
#include "key.h"
#include "RPN.h"
#include "settings.h"
#include "macro.h"
#include "pico/stdlib.h"
#include "settings.h"

// メニューアイテムタイプ
typedef enum
{
    MI_SUBMENU, // サブメニュー
    MI_ACTION,  // アクション
    MI_ENUM,    // 列挙値選択
    MI_VALUE,   // 数値入力
    MI_TOGGLE   // ON/OFF切り替え
} menu_item_type_t;

// メニューアイテム構造体
typedef struct menu_item_s menu_item_t;
struct menu_item_s
{
    const char *title;     // 表示名
    menu_item_type_t type; // タイプ

    // サブメニュー用
    const menu_item_t *children;
    int child_count;

    // 列挙値/トグル用
    int (*getter)(void);
    void (*setter)(int);
    const char *const *enum_labels;
    int enum_count;
    int min_value;
    int max_value;

    // アクション用
    void (*action)(void);

    // 表示用
    const char *description; // 説明テキスト
};

// メニューフレーム（スタック用）
typedef struct
{
    const menu_item_t *menu; // 現在のメニュー
    int index;               // 選択インデックス
    int scroll;              // スクロール位置
} menu_frame_t;

// メニューシステム状態
static struct
{
    bool open;
    menu_frame_t stack[6]; // スタック深度を増加
    int depth;
    bool redraw_needed; // 再描画フラグ
} g_menu;

// 現在のフレーム取得
static inline menu_frame_t *current_frame(void)
{
    return (g_menu.depth > 0) ? &g_menu.stack[g_menu.depth - 1] : NULL;
}

// 設定値のゲッター/セッター
static int get_angle_mode(void) { return (int)rpn_get_angle_mode(); }
static void set_angle_mode(int v) { rpn_set_angle_mode((angle_mode_t)v); }
static int get_disp_mode(void) { return (int)rpn_get_disp_mode(); }
static void set_disp_mode(int v) { rpn_set_disp_mode((disp_mode_t)v); }
static int get_hyper_mode(void) { return (int)rpn_get_hyperbolic_mode(); }
static void set_hyper_mode(int v) { rpn_set_hyperbolic_mode((hyperbolic_mode_t)v); }

// Digits （ALL,0..9）
static int get_digits_enum(void)
{
    int8_t d = settings_get_digits();
    return (d < 0) ? 0 : (1 + d);
}
static void set_digits_enum(int idx)
{
    if (idx <= 0)
        settings_set_digits(-1);
    else
        settings_set_digits((int8_t)(idx - 1));
}
// Last Key mode
static int get_last_key_mode_enum(void) { return (int)settings_get_last_key_mode(); }
static void set_last_key_mode_enum(int v) { settings_set_last_key_mode((last_key_mode_t)v); }
// Resume toggle
static int get_resume_enum(void) { return settings_get_resume_enabled() ? 1 : 0; }
static void set_resume_enum(int v) { settings_set_resume_enabled(v ? true : false); }

// アクション関数
static void action_reset_calculator(void)
{
    // 最終確認ダイアログ
    key_set_shift_state(false);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_str("Reset ALL ?     ");
    lcd_set_cursor(1, 0);
    lcd_write_str("ENTER=Yes DEL=No");

    // キー入力待ち（ENTERで実行／CLR,DEL,OFFでキャンセル）
    while (1)
    {
        key_event_t ev = key_poll();
        if (ev.type == KEY_EVENT_DOWN)
        {
            if (ev.code == K_ENTER)
            {
                break; // 確定して実行へ
            }
            if (ev.code == K_DEL || ev.code == K_OFF)
            {
                // キャンセル：メニューに戻る
                g_menu.redraw_needed = true;
                return;
            }
        }
        sleep_ms(10);
    }

    // リセット処理中はキー掃引を停止し、安全にフラッシュ操作
    key_scan_pause();
    settings_reset_to_defaults();
    macro_reset_all();

    // RPN 実体の状態も初期化（フラッシュから既定値を再読込）
    init_rpn();

    // 入力/キー状態の残渣をクリアしてから再開
    key_reset();
    key_set_shift_state(false);
    key_scan_resume();

    // 完了表示を短時間出す
    lcd_set_cursor(0, 0);
    lcd_write_str("   Reset Done   ");
    lcd_set_cursor(1, 0);
    lcd_write_str("                ");
    sleep_ms(500);

    menu_close();
}

// コントラストUI表示ヘルパ
static void render_contrast_screen(int v)
{
    char line1[17] = {0};
    char line2[17] = {0};
    // タイトル（固定）
    memcpy(line1, "LCD Contrast   ", 16);
    // 値と操作ヒント
    snprintf(line2, sizeof(line2), "Val:%2d +/- ENT", v);
    for (int i = (int)strlen(line2); i < 16; ++i)
        line2[i] = ' ';

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write(line1, 16);
    lcd_set_cursor(1, 0);
    lcd_write(line2, 16);
}

// LCDコントラスト詳細UI
static void action_adjust_contrast(void)
{
    // 現在値を取得し、キャンセル用に退避
    int value = (int)settings_get_lcd_contrast();
    int original = value;

    // 初期描画
    render_contrast_screen(value);

    // 入力ループ
    while (1)
    {
        key_event_t ev = key_poll();
        if (ev.type != KEY_EVENT_DOWN && ev.type != KEY_EVENT_REPEAT)
        {
            sleep_ms(10);
            continue;
        }

        if (ev.code == K_ADD || ev.code == K_ROLL)
        {
            if (value < 50)
                value++;
            settings_set_lcd_contrast((uint8_t)value);
            lcd_set_contrast((uint8_t)value);
            render_contrast_screen(value);
            continue;
        }
        if (ev.code == K_SUB || ev.code == K_ROLLUP)
        {
            if (value > 25)
                value--;
            settings_set_lcd_contrast((uint8_t)value);
            lcd_set_contrast((uint8_t)value);
            render_contrast_screen(value);
            continue;
        }

        if (ev.code == K_ENTER)
        {
            // 確定してメニューへ戻る（値はすでに反映済み）
            key_set_shift_state(false);
            break;
        }
        if (ev.code == K_DEL || ev.code == K_CLR || ev.code == K_OFF)
        {
            // キャンセル：元の値へ戻して終了
            // 念のため範囲に丸めて戻す
            int restore = original;
            if (restore < 25)
                restore = 25;
            if (restore > 50)
                restore = 50;
            settings_set_lcd_contrast((uint8_t)restore);
            lcd_set_contrast((uint8_t)restore);
            key_set_shift_state(false);
            break;
        }
    }

    // メニュー再描画要求
    g_menu.redraw_needed = true;
}

// メニュー矢印を表示すべき項目か判定（サブメニュー or コントラスト調整）
static inline bool menu_item_has_arrow(const menu_item_t *item)
{
    return item && (item->type == MI_SUBMENU || item->action == action_adjust_contrast);
}

static void action_about(void)
{
    // バージョン情報を表示し、任意のキーで戻る
    lcd_clear();

    char line1[17];
    char line2[17];
    for (int i = 0; i < 16; ++i)
    {
        line1[i] = ' ';
        line2[i] = ' ';
    }
    line1[16] = '\0';
    line2[16] = '\0';

    // 1行目: プログラム名 + 版本号
    // CmakeLists.txt で定義
#ifdef PICO_PROGRAM_NAME
#ifdef PICO_PROGRAM_VERSION_STRING
    snprintf(line1, sizeof(line1), "%s:v%s", PICO_PROGRAM_NAME, PICO_PROGRAM_VERSION_STRING);
#else
    snprintf(line1, sizeof(line1), "%s", PICO_PROGRAM_NAME);
#endif
#else
    snprintf(line1, sizeof(line1), "RPN35 About");
#endif
    // パディング
    for (int i = (int)strlen(line1); i < 16; ++i)
        line1[i] = ' ';

    // 2行目: ビルド日付 (yyyy/mm/dd)
    {
        const char *d = __DATE__; // 例: "Sep  6 2025" / "Sep 16 2025"
        int day = (d[4] == ' ') ? (d[5] - '0') : ((d[4] - '0') * 10 + (d[5] - '0'));
        int year = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
        int month = 0;
        switch (d[0])
        {
        case 'J':
            month = (d[1] == 'a') ? 1 : (d[2] == 'n' ? 6 : 7); // Jan / Jun / Jul
            break;
        case 'F':
            month = 2; // Feb
            break;
        case 'M':
            month = (d[2] == 'r') ? 3 : 5; // Mar / May
            break;
        case 'A':
            month = (d[1] == 'p') ? 4 : 8; // Apr / Aug
            break;
        case 'S':
            month = 9; // Sep
            break;
        case 'O':
            month = 10; // Oct
            break;
        case 'N':
            month = 11; // Nov
            break;
        case 'D':
            month = 12; // Dec
            break;
        default:
            month = 0; // 未知
            break;
        }

        snprintf(line2, sizeof(line2), "Date:%04d/%02d/%02d", year, month, day);
        for (int i = (int)strlen(line2); i < 16; ++i)
            line2[i] = ' ';
    }

    lcd_set_cursor(0, 0);
    lcd_write(line1, 16);
    lcd_set_cursor(1, 0);
    lcd_write(line2, 16);

    // いずれかのキー押下で戻る（メニューは開いたままなので、戻るとメニュー再描画）
    while (1)
    {
        key_event_t ev = key_poll();
        if (ev.type == KEY_EVENT_DOWN)
            break;
        sleep_ms(10);
    }
}

static void action_exit_menu(void)
{
    menu_close();
}

// 列挙値ラベル
static const char *const angle_labels[] = {"DEG", "RAD", "GRAD"};
static const char *const disp_labels[] = {"NORM", "SCI", "ENG"};
static const char *const digits_labels[] = {"ALL", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
static const char *const hyper_labels[] = {"OFF", "ON"};
static int get_auto_off_mode(void) { return (int)settings_get_auto_off_mode(); }
static void set_auto_off_mode(int v) { settings_set_auto_off_mode((auto_off_mode_t)v); }
static const char *const auto_off_labels[] = {"OFF", "3m", "5m", "10m"};
static int get_lcd_contrast(void) { return (int)settings_get_lcd_contrast(); }
static void set_lcd_contrast(int v)
{
    settings_set_lcd_contrast((uint8_t)v);
    lcd_set_contrast((uint8_t)v);
}

// サブメニュー定義
static const menu_item_t settings_items[] = {
    {"Ang. Unit", MI_ENUM, NULL, 0, get_angle_mode, set_angle_mode, angle_labels, 3, 0, 0, NULL, "Angle unit"},
    {"Display", MI_ENUM, NULL, 0, get_disp_mode, set_disp_mode, disp_labels, 3, 0, 0, NULL, "Display format"},
    {"Digits", MI_ENUM, NULL, 0, get_digits_enum, set_digits_enum, digits_labels, 11, 0, 0, NULL, "Fraction digits"},
    {"Hyp. Mode", MI_ENUM, NULL, 0, get_hyper_mode, set_hyper_mode, hyper_labels, 2, 0, 0, NULL, "Hyperbolic trig mode"},
};

// Reset submenu actions
static void action_reset_stack(void)
{
    rpn_reset_stack_only();
    menu_close();
}
static void action_reset_vars(void)
{
    rpn_reset_vars_only();
    menu_close();
}
static void action_reset_memory(void)
{
    rpn_reset_memory();
    menu_close();
}

static const menu_item_t reset_items[] = {
    {"Stack", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_reset_stack, "Clear stack & Last X"},
    {"Vars", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_reset_vars, "Clear A..F vars"},
    {"Stack&Vars", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_reset_memory, "Clear Stack+Vars"},
    {"FactoryReset", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_reset_calculator, "Factory reset"},
};

static const menu_item_t system_items[] = {
    {"Auto Off", MI_ENUM, NULL, 0, get_auto_off_mode, set_auto_off_mode, auto_off_labels, 4, 0, 0, NULL, "Auto power-off"},
    {"Resume", MI_ENUM, NULL, 0, get_resume_enum, set_resume_enum, hyper_labels, 2, 0, 0, NULL, "Resume on boot"},
    {"Last Key", MI_ENUM, NULL, 0, get_last_key_mode_enum, set_last_key_mode_enum, (const char *const[]){"Last X", "Undo"}, 2, 0, 0, NULL, "Last key behavior"},
    {"LCD Contrast", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_adjust_contrast, "Adjust LCD contrast"},
    {"Reset", MI_SUBMENU, reset_items, sizeof(reset_items) / sizeof(reset_items[0]), NULL, NULL, NULL, 0, 0, 0, NULL, "Reset submenu"},
    {"About", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_about, "About this calculator"},
};

static const menu_item_t main_items[] = {
    {"Settings", MI_SUBMENU, settings_items, sizeof(settings_items) / sizeof(settings_items[0]), NULL, NULL, NULL, 0, 0, 0, NULL, "Calculator settings"},
    {"System", MI_SUBMENU, system_items, sizeof(system_items) / sizeof(system_items[0]), NULL, NULL, NULL, 0, 0, 0, NULL, "System functions"},
    {"Exit", MI_ACTION, NULL, 0, NULL, NULL, NULL, 0, 0, 0, action_exit_menu, "Exit menu"},
};

static const menu_item_t root_menu = {
    "Main Menu", MI_SUBMENU, main_items, sizeof(main_items) / sizeof(main_items[0]),
    NULL, NULL, NULL, 0, 0, 0, NULL, "RPN35 Main Menu"};

// メニューシステム初期化
void menu_init(void)
{
    memset(&g_menu, 0, sizeof(g_menu));
}

// メニューオープン
void menu_open(void)
{
    key_set_shift_state(false);
    g_menu.open = true;
    g_menu.depth = 1;
    g_menu.stack[0].menu = &root_menu;
    g_menu.stack[0].index = 0;
    g_menu.stack[0].scroll = 0;
    g_menu.redraw_needed = true;
}

// メニュークローズ
void menu_close(void)
{
    g_menu.open = false;
    g_menu.depth = 0;
    key_set_shift_state(false); // シフト状態をクリア
}

// メニューオープン状態確認
bool menu_is_open(void)
{
    return g_menu.open;
}

// 文字列を指定幅でコピー（パディング付き）
static void copy_padded(char *dest, const char *src, int width)
{
    int i = 0;
    if (src)
    {
        while (i < width && src[i])
        {
            dest[i] = src[i];
            i++;
        }
    }
    while (i < width)
    {
        dest[i] = ' ';
        i++;
    }
}

// メニュー描画
void menu_render(void)
{
    if (!g_menu.open)
        return;

    menu_frame_t *frame = current_frame();
    if (!frame || !frame->menu)
        return;

    const menu_item_t *menu = frame->menu;
    char line1[17] = {0};
    char line2[17] = {0};

    // 階層の深さに応じた記号を生成
    char depth_prefix[5] = {0}; // 最大4階層まで（">>>>"）
    int prefix_len = 0;
    if (g_menu.depth > 1)
    {
        // サブメニューの場合は深さに応じて'>'を追加
        for (int i = 0; i < g_menu.depth - 1 && i < 4; i++)
        {
            depth_prefix[prefix_len++] = '>';
        }
    }

    // 2行表示（ルートメニューもサブメニューも同じ方式）
    int item1_idx = frame->scroll;
    int item2_idx = frame->scroll + 1;

    // 1行目の項目
    if (item1_idx < menu->child_count)
    {
        const menu_item_t *item1 = &menu->children[item1_idx];
        int pos = 0;

        // 選択インジケータまたはスペース
        if (frame->index == item1_idx)
        {
            line1[pos++] = '>';
        }
        else
        {
            line1[pos++] = ' ';
        }

        // 深さ記号を追加
        for (int i = 0; i < prefix_len && pos < 15; i++)
        {
            line1[pos++] = depth_prefix[i];
        }

        // 項目名を表示
        const char *title = item1->title;
        while (pos < 15 && title && *title)
        {
            line1[pos++] = *title++;
        }

        // 値表示（列挙型/数値型の場合）
        if (item1->type == MI_ENUM && item1->getter && item1->enum_labels)
        {
            int value = item1->getter();
            if (value >= 0 && value < item1->enum_count && pos < 15)
            {
                line1[pos++] = ':';
                const char *label = item1->enum_labels[value];
                while (pos < 15 && label && *label)
                {
                    line1[pos++] = *label++;
                }
            }
        }
        else if (item1->type == MI_VALUE && item1->getter)
        {
            int v = item1->getter();
            if (pos < 15)
            {
                line1[pos++] = ':';
                char buf[6] = {0};
                snprintf(buf, sizeof(buf), "%d", v);
                const char *p = buf;
                while (pos < 15 && *p)
                {
                    line1[pos++] = *p++;
                }
            }
        }

        // パディング
        while (pos < 16)
            line1[pos++] = ' ';
    }
    else
    {
        copy_padded(line1, "", 16);
    }

    // 2行目の項目
    if (item2_idx < menu->child_count)
    {
        const menu_item_t *item2 = &menu->children[item2_idx];
        int pos = 0;

        // 選択インジケータまたはスペース
        if (frame->index == item2_idx)
        {
            line2[pos++] = '>';
        }
        else
        {
            line2[pos++] = ' ';
        }

        // 深さ記号を追加
        for (int i = 0; i < prefix_len && pos < 15; i++)
        {
            line2[pos++] = depth_prefix[i];
        }

        // 項目名を表示
        const char *title = item2->title;
        while (pos < 15 && title && *title)
        {
            line2[pos++] = *title++;
        }

        // 値表示（列挙型/数値型の場合）
        if (item2->type == MI_ENUM && item2->getter && item2->enum_labels)
        {
            int value = item2->getter();
            if (value >= 0 && value < item2->enum_count && pos < 15)
            {
                line2[pos++] = ':';
                const char *label = item2->enum_labels[value];
                while (pos < 15 && label && *label)
                {
                    line2[pos++] = *label++;
                }
            }
        }
        else if (item2->type == MI_VALUE && item2->getter)
        {
            int v = item2->getter();
            if (pos < 15)
            {
                line2[pos++] = ':';
                char buf[6] = {0};
                snprintf(buf, sizeof(buf), "%d", v);
                const char *p = buf;
                while (pos < 15 && *p)
                {
                    line2[pos++] = *p++;
                }
            }
        }

        // パディング
        while (pos < 16)
            line2[pos++] = ' ';
    }
    else
    {
        copy_padded(line2, "", 16);
    }

    // スクロールインジケータを1行目の右端に常に表示
    bool shift_active = key_get_shift_state();
    if (menu->child_count > 0)
    {
        if (shift_active)
        {
            // シフト状態：上方向スクロール
            line1[15] = LCD_CHAR_UP_ARROW;
        }
        else
        {
            // 通常状態：下方向スクロール
            line1[15] = LCD_CHAR_DOWN_ARROW;
        }
    }

    // サブメニュー矢印を該当する行に表示
    if (item1_idx < menu->child_count && frame->index == item1_idx)
    {
        const menu_item_t *selected = &menu->children[item1_idx];
        if (menu_item_has_arrow(selected))
        {
            line1[14] = LCD_CHAR_MENU_ARROW;
        }
    }
    if (item2_idx < menu->child_count && frame->index == item2_idx)
    {
        const menu_item_t *selected = &menu->children[item2_idx];
        if (menu_item_has_arrow(selected))
        {
            line2[14] = LCD_CHAR_MENU_ARROW;
        }
    }

    // LCD出力
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write(line1, 16);
    lcd_set_cursor(1, 0);
    lcd_write(line2, 16);

    g_menu.redraw_needed = false;
}

// キーハンドリング
bool menu_handle_key(key_event_t ev)
{
    if (ev.type == KEY_EVENT_NONE || ev.type == KEY_EVENT_UP)
        return false;
    if (!g_menu.open)
        return false;

    menu_frame_t *frame = current_frame();
    if (!frame || !frame->menu)
        return false;

    const menu_item_t *menu = frame->menu;

    // 戻るキー
    if (ev.code == K_CLR || ev.code == K_DEL || (ev.code == K_OFF && ev.type == KEY_EVENT_DOWN))
    {
        if (g_menu.depth > 1)
        {
            g_menu.depth--;
            // メニュー階層を上がったらシフト解除
            key_set_shift_state(false);
            g_menu.redraw_needed = true;
        }
        else
        {
            menu_close();
        }
        return true;
    }

    // シフトキーが押された時は再描画（矢印の向きが変わるため）
    if (ev.code == K_SHIFT)
    {
        g_menu.redraw_needed = true;
        return true;
    }

    // ナビゲーション（上下移動）
    if (ev.code == K_ROLL)
    { // 下方向
        if (frame->index + 1 < menu->child_count)
        {
            frame->index++;

            // 2行表示でのスクロール調整（全メニューレベル共通）
            // 選択項目が表示範囲外になったらスクロール
            if (frame->index >= frame->scroll + 2)
            {
                frame->scroll = frame->index - 1;
            }
            g_menu.redraw_needed = true;
        }
        return true;
    }
    if (ev.code == K_ROLLUP)
    { // 上方向
        if (frame->index > 0)
        {
            frame->index--;

            // 2行表示でのスクロール調整（全メニューレベル共通）
            // 選択項目が表示範囲外になったらスクロール
            if (frame->index < frame->scroll)
            {
                frame->scroll = frame->index;
            }
            g_menu.redraw_needed = true;
        }
        return true;
    }

    // 現在選択中の項目
    if (frame->index >= menu->child_count)
        return false;
    const menu_item_t *selected = &menu->children[frame->index];

    // 列挙値/数値の変更
    if ((selected->type == MI_ENUM || selected->type == MI_VALUE) && selected->getter && selected->setter)
    {
        if (ev.code == K_ADD || ev.code == K_SUB)
        {
            int value = selected->getter();
            if (selected->type == MI_ENUM)
            {
                if (ev.code == K_ADD)
                {
                    value = (value + 1) % selected->enum_count;
                }
                else
                {
                    value = (value - 1 + selected->enum_count) % selected->enum_count;
                }
                selected->setter(value);
            }
            else
            {
                // 数値: 範囲[min,max]内で変化
                if (ev.code == K_ADD && value < selected->max_value)
                    value++;
                if (ev.code == K_SUB && value > selected->min_value)
                    value--;
                if (value < selected->min_value)
                    value = selected->min_value;
                if (value > selected->max_value)
                    value = selected->max_value;
                selected->setter(value);
            }
            g_menu.redraw_needed = true;
            return true;
        }
    }

    // 決定キー
    if (ev.code == K_ENTER)
    {
        if (selected->type == MI_SUBMENU && selected->children && selected->child_count > 0)
        {
            // サブメニューに入る
            if (g_menu.depth < (int)(sizeof(g_menu.stack) / sizeof(g_menu.stack[0])))
            {
                g_menu.stack[g_menu.depth].menu = selected;
                g_menu.stack[g_menu.depth].index = 0;
                g_menu.stack[g_menu.depth].scroll = 0;
                g_menu.depth++;
                // メニュー階層を移動したらシフト解除
                key_set_shift_state(false);
                g_menu.redraw_needed = true;
            }
            return true;
        }
        else if (selected->type == MI_ACTION && selected->action)
        {
            // アクションを実行
            selected->action();
            g_menu.redraw_needed = true;
            return true;
        }
        else if (selected->type == MI_ENUM && selected->getter && selected->setter)
        {
            // ENTERで次の値に進む
            int value = (selected->getter() + 1) % selected->enum_count;
            selected->setter(value);
            g_menu.redraw_needed = true;
            return true;
        }
        else if (selected->type == MI_VALUE && selected->getter && selected->setter)
        {
            // ENTERで +1（上限まで）
            int value = selected->getter();
            if (value < selected->max_value)
                value++;
            selected->setter(value);
            g_menu.redraw_needed = true;
            return true;
        }
    }

    return false;
}

// 再描画が必要かチェック
bool menu_needs_redraw(void)
{
    return g_menu.redraw_needed;
}

// 再描画フラグをクリア
void menu_clear_redraw_flag(void)
{
    g_menu.redraw_needed = false;
}
