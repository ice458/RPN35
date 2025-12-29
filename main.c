#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "clock_ctrl.h"
#include "hardware/vreg.h"
#include "hardware_definition.h"

#include "LCD.h"
#include "key.h"
#include "RPN.h"
#include "menu.h"
#include "settings.h"
#include "macro.h"
#include "ui_const.h"
#include "ui_macro.h"
#include "resume.h"

void power_down_seq(void)
{
    key_scan_pause();
    clockctrl_enter_high_speed_12mhz();
    lcd_clear();
    lcd_set_cursor(1, 0);
    lcd_write_str("    See you!    ");
    settings_save_if_dirty();
    macro_save_if_dirty();
    resume_save_if_enabled();
    sleep_ms(500);
    POWER_DOWN;
}

// SHOWモード（Xを2行で表示）
static bool g_show_mode = false;

// 自動電源OFF: 設定から取得（0=無効）
static inline uint32_t auto_off_ms_setting(void) { return settings_get_auto_off_ms(); }
// クロック低速化(無操作5秒)
#define IDLE_TO_LOW_MS (5u * 1000u)

// 最終操作時刻（to_ms_since_boot() ベース）
static uint32_t g_last_activity_ms = 0;
// 低電力モード中かどうか
static bool g_low_power = false;

// 変数メモリ: VA..VF（6個）
static int var_index_from_key(key_code_t code)
{
    switch (code)
    {
    case K_VA:
        return 0;
    case K_VB:
        return 1;
    case K_VC:
        return 2;
    case K_VD:
        return 3;
    case K_VE:
        return 4;
    case K_VF:
        return 5;
    default:
        return -1;
    }
}

// 変数操作シーケンスのハンドリング
static bool handle_var_sequence(key_event_t ev)
{
    if (ev.type == KEY_EVENT_NONE || ev.type == KEY_EVENT_UP)
        return false;
    // オペレータ確定
    if (ev.code == K_ST)
    {
        rpn_var_set_pending_op(RPN_VAR_OP_ST);
        key_set_shift_state(true);
        return true;
    }
    if (ev.code == K_LD)
    {
        rpn_var_set_pending_op(RPN_VAR_OP_LD);
        key_set_shift_state(true);
        return true;
    }
    if (ev.code == K_CLR)
    {
        rpn_var_set_pending_op(RPN_VAR_OP_CLR);
        key_set_shift_state(true);
        return true;
    }
    // 変数キーなら実行
    int idx = var_index_from_key(ev.code);
    if (idx >= 0)
    {
        // pending_opが未設定なら、単押しはロードとして扱う
        if (rpn_var_get_pending_op() == RPN_VAR_OP_NONE)
        {
            rpn_var_set_pending_op(RPN_VAR_OP_LD);
        }
        bool res = rpn_var_apply_slot(idx);
        // 変数操作が完了してモードを抜けたらシフト解除
        if (rpn_var_get_pending_op() == RPN_VAR_OP_NONE)
        {
            key_set_shift_state(false);
        }
        return res;
    }
    return false;
}

// クロックを低速モードに切り替え
static void enter_low_power_clock(void)
{
    key_scan_pause();
    clockctrl_enter_low_power();
    g_last_activity_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    key_scan_resume();
    g_low_power = true;
}

// クロックを高速モードに切り替え
static void enter_high_speed_clock(void)
{
    key_scan_pause();
    clockctrl_enter_high_speed_12mhz();
    g_last_activity_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    key_scan_resume();
    g_low_power = false;
}

// 表示更新
static void refresh_display(void)
{
    char line[17];
    char buf[40];

    // SHOWモード中はXを32桁（2行）に丸めて表示（入力中でも確定値を表示）
    if (g_show_mode)
    {
        BID_UINT128 x = rpn_stack_x();
        char buf32[33];
        bid128_to_str(x, buf32, sizeof(buf32)); // 32桁に収まるよう丸め（末尾0保持）

        // 上段（先頭16文字）
        for (int i = 0; i < 16; ++i)
            line[i] = ' ';
        for (int i = 0; i < 16 && buf32[i] != '\0'; ++i)
            line[i] = buf32[i];
        lcd_set_cursor(0, 0);
        lcd_write(line, 16);

        // 下段（次の16文字）
        for (int i = 0; i < 16; ++i)
            line[i] = ' ';
        for (int i = 0; i < 16 && buf32[i + 16] != '\0'; ++i)
            line[i] = buf32[i + 16];
        lcd_set_cursor(1, 0);
        lcd_write(line, 16);
        return;
    }

    // 1行目: X（入力中は生文字列を右寄せスクロール表示、確定時は整形表示）
    for (int i = 0; i < 16; ++i)
        line[i] = ' ';
    if (rpn_is_input_active())
    {
        char raw[64];
        int rawlen = rpn_get_input_string(raw, sizeof(raw));
        if (rawlen <= 16)
        {
            // 収まる場合は左寄せ
            for (int i = 0; i < rawlen; ++i)
                line[i] = raw[i];
        }
        else
        {
            // 収まらない場合のみ、右端に最新桁が来るようスクロール表示
            int take = 16;
            for (int i = 0; i < take; ++i)
            {
                line[16 - take + i] = raw[rawlen - take + i];
            }
        }
    }
    else
    {
        // 非入力時は表示幅(16桁)に収まるように丸めた文字列を生成
        BID_UINT128 x = rpn_stack_x();
        char buf16[17];
        bid128_to_str(x, buf16, sizeof(buf16));
        for (int i = 0; i < 16 && buf16[i] != '\0'; ++i)
            line[i] = buf16[i];
    }
    // 下段（X）右端にマクロ状態インジケータを表示
    if (macro_is_recording())
    {
        line[15] = 'R';
    }
    else if (macro_is_playing())
    {
        line[15] = 'P';
    }
    lcd_set_cursor(1, 0);
    lcd_write(line, 16);

    // 2行目: Y（右端にインジケータ: Shift='s' と 変数オペレータ）
    BID_UINT128 y = rpn_stack_y();
    char ybuf16[17];
    bid128_to_str(y, ybuf16, sizeof(ybuf16));
    for (int i = 0; i < 16; ++i)
        line[i] = ' ';
    for (int i = 0; i < 16 && ybuf16[i] != '\0'; ++i)
        line[i] = ybuf16[i];
    // 右端にインジケータ
    char opch = rpn_var_indicator_char();
    if (opch != '\0')
    {
        line[15] = opch; // 最優先: 変数オペレータ 'S','L','C'
    }
    else if (key_get_shift_state())
    {
        line[15] = 's'; // 記録中でなければシフト表示
    }
    lcd_set_cursor(0, 0);
    lcd_write(line, 16);
}

// キー動作割り当て（true: 画面更新要）
static bool handle_key(key_event_t ev)
{
    if (ev.type == KEY_EVENT_NONE)
        return false;
    if (ev.type == KEY_EVENT_UP)
        return false; // DOWN/REPEATのみを処理

    // 科学定数UIが開いている場合
    if (const_ui_is_open())
    {
        return const_ui_handle_key(ev);
    }
    // マクロUIが開いている場合
    if (macro_ui_is_open())
    {
        return macro_ui_handle_key(ev);
    }

    // 変数操作のシーケンス
    if (handle_var_sequence(ev))
        return true;
    // 変数操作モード中のキャンセル（DEL/OFF）を処理（シフト解除）
    if (rpn_var_get_pending_op() != RPN_VAR_OP_NONE && (ev.code == K_DEL || ev.code == K_OFF))
    {
        rpn_var_set_pending_op(RPN_VAR_OP_NONE);
        key_set_shift_state(false);
        return true;
    }
    // 変数操作モード中は常にシフトONを強制（インジケータはオペレータ優先）
    if (rpn_var_get_pending_op() != RPN_VAR_OP_NONE)
    {
        key_set_shift_state(true);
    }
    // 変数操作モード中は、変数名（VA..VF）以外のキーを受け付けない
    if (rpn_var_get_pending_op() != RPN_VAR_OP_NONE)
    {
        int vidx = var_index_from_key(ev.code);
        if (vidx < 0)
        {
            // 変数名以外は無視
            return false;
        }
    }
    switch (ev.code)
    {
    // シフトキー（トグル状態を画面に即反映）
    case K_SHIFT:
        return true;
    // 数値入力
    case K_0:
        rpn_input_append_digit('0');
        return true;
    case K_1:
        rpn_input_append_digit('1');
        return true;
    case K_2:
        rpn_input_append_digit('2');
        return true;
    case K_3:
        rpn_input_append_digit('3');
        return true;
    case K_4:
        rpn_input_append_digit('4');
        return true;
    case K_5:
        rpn_input_append_digit('5');
        return true;
    case K_6:
        rpn_input_append_digit('6');
        return true;
    case K_7:
        rpn_input_append_digit('7');
        return true;
    case K_8:
        rpn_input_append_digit('8');
        return true;
    case K_9:
        rpn_input_append_digit('9');
        return true;
    case K_DOT:
        rpn_input_dot();
        return true;
    case K_EE:
        rpn_input_exp();
        return true;
    case K_SIGN:
        rpn_input_toggle_sign();
        return true;
    case K_DEL:
    {
        if (rpn_is_input_active())
        {
            rpn_input_backspace();
        }
        else
        {
            rpn_clear_x();
        }
        return true;
    }
    case K_ENTER:
        rpn_enter();
        return true;

    // 四則
    case K_ADD:
        rpn_add();
        return true;
    case K_SUB:
        rpn_sub();
        return true;
    case K_MUL:
        rpn_mul();
        return true;
    case K_DIV:
        rpn_div();
        return true;

    // スタック操作
    case K_SWAP:
        if (rpn_is_input_active())
        {
            rpn_commit_input_without_push(); // 数値入力を先に確定（pushしない）
        }
        rpn_swap();
        return true;
    case K_ROLL:
        if (rpn_is_input_active())
        {
            rpn_commit_input_without_push();
        }
        rpn_roll_down();
        return true;
    case K_ROLLUP:
        if (rpn_is_input_active())
        {
            rpn_commit_input_without_push();
        }
        rpn_roll_up();
        return true;

    // 単項/二項関数
    case K_SQRT:
        rpn_sqrt();
        return true;
    case K_POW2:
        rpn_pow2();
        return true;
    case K_POW3:
        rpn_cube();
        return true;
    case K_CUBE_ROOT:
        rpn_cbrt();
        return true;
    case K_NTH_ROOT:
        rpn_nth_root();
        return true;
    case K_POW:
        rpn_pow();
        return true;
    case K_LOG:
        rpn_log();
        return true;
    case K_LN:
        rpn_ln();
        return true;
    case K_LOGXY:
        rpn_logxy();
        return true;
    case K_EXP:
        rpn_exp();
        return true;
    case K_POW10:
        rpn_exp10();
        return true;
    case K_FACT:
        rpn_fact();
        return true;
    case K_REV:
        rpn_rev();
        return true;

    // 三角関数（HyperbolicモードがONなら双曲線関数に切替）
    case K_SIN:
        if (rpn_get_hyperbolic_mode() == HYPERBOLIC_MODE_ON)
            rpn_sinh();
        else
            rpn_sin();
        return true;
    case K_COS:
        if (rpn_get_hyperbolic_mode() == HYPERBOLIC_MODE_ON)
            rpn_cosh();
        else
            rpn_cos();
        return true;
    case K_TAN:
        if (rpn_get_hyperbolic_mode() == HYPERBOLIC_MODE_ON)
            rpn_tanh();
        else
            rpn_tan();
        return true;
    case K_ASIN:
        if (rpn_get_hyperbolic_mode() == HYPERBOLIC_MODE_ON)
            rpn_asinh();
        else
            rpn_asin();
        return true;
    case K_ACOS:
        if (rpn_get_hyperbolic_mode() == HYPERBOLIC_MODE_ON)
            rpn_acosh();
        else
            rpn_acos();
        return true;
    case K_ATAN:
        if (rpn_get_hyperbolic_mode() == HYPERBOLIC_MODE_ON)
            rpn_atanh();
        else
            rpn_atan();
        return true;

    // 定数
    case K_PI:
        rpn_input_pi();
        // 定数入力後はシフト解除
        key_set_shift_state(false);
        return true;
    case K_e:
        rpn_input_e();
        key_set_shift_state(false);
        return true;

    // マクロ（P1..P3 再生、PRで設定UI）
    case K_P1:
        if (macro_is_recording())
        {
            // 録画中は再生キーを無視（録画を中断しない／シフト状態も保持）
            return false;
        }
        else
        {
            // Undoモード時はマクロ開始直前に境界スナップショットを1回だけ取る
            if (settings_get_last_key_mode() == LAST_KEY_UNDO)
            {
                rpn_undo_capture_boundary();
            }
            bool started = macro_play(0);
            key_set_shift_state(false);
            return started ? false : true; // 再生時は注入イベントで描画
        }
    case K_P2:
        if (macro_is_recording())
        {
            return false;
        }
        else
        {
            if (settings_get_last_key_mode() == LAST_KEY_UNDO)
            {
                rpn_undo_capture_boundary();
            }
            bool started = macro_play(1);
            key_set_shift_state(false);
            return started ? false : true;
        }
    case K_P3:
        if (macro_is_recording())
        {
            return false;
        }
        else
        {
            if (settings_get_last_key_mode() == LAST_KEY_UNDO)
            {
                rpn_undo_capture_boundary();
            }
            bool started = macro_play(2);
            key_set_shift_state(false);
            return started ? false : true;
        }
    case K_PR:
        if (macro_is_recording())
        {
            macro_stop_record();
            key_set_shift_state(false);
            macro_ui_open();
        }
        else
        {
            key_set_shift_state(false);
            macro_ui_open();
        }
        return false; // 直接描画済み

    // 表示/モード
    case K_DISP:
    {
        disp_mode_t m = rpn_get_disp_mode();
        m = (m == DISP_MODE_ENGINEERING) ? DISP_MODE_NORMAL : (disp_mode_t)(m + 1);
        rpn_set_disp_mode(m);
        return true;
    }
    case K_MODE:
    {
        angle_mode_t a = rpn_get_angle_mode();
        a = (a == ANGLE_MODE_GRAD) ? ANGLE_MODE_DEG : (angle_mode_t)(a + 1);
        rpn_set_angle_mode(a);
        return true;
    }
    case K_SHOW:
    {
        // SHOW表示をトグル
        g_show_mode = !g_show_mode;
        return true;
    }

    // 科学定数UI（K_C1/K_C2）
    case K_C1:
        const_ui_open(1);
        return false; // 直接描画済み
    case K_C2:
        const_ui_open(2);
        return false;

    // Last
    case K_LAST:
        if (settings_get_last_key_mode() == LAST_KEY_UNDO)
        {
            rpn_undo();
        }
        else
        {
            rpn_last();
        }
        // 実行後はシフト解除
        key_set_shift_state(false);
        return true;

    // 電源OFF
    case K_OFF:
        if (ev.type == KEY_EVENT_DOWN)
        {
            power_down_seq();
        }
        return false;

    default:
        return false;
    }
}

int main(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_00);
    sleep_ms(1);

    clock_configure_undivided(clk_ref,
                              CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                              0,
                              12 * MHZ);

    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              12 * MHZ);
    pll_deinit(pll_usb);
    pll_deinit(pll_sys);

    // PLLは無効なのでクロックは供給されない
    clock_configure_undivided(clk_peri,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                              0,
                              0);
    clock_configure_undivided(clk_adc,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                              0,
                              0);
    clock_configure_undivided(clk_usb,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                              0,
                              0);
    clock_configure_undivided(clk_hstx,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                              0,
                              0);

    // 電源ラッチ
    gpio_init(POWER_EN);
    gpio_set_dir(POWER_EN, GPIO_OUT);
    gpio_put(POWER_EN, 1);

    // LCD初期化
    lcd_init();

    // 設定読み込み
    settings_init();

    // コントラスト安全クランプ（25〜50に固定）
    uint8_t cv = settings_get_lcd_contrast();
    uint8_t safe = cv;
    if (safe < 25)
        safe = 25;
    if (safe > 50)
        safe = 50;
    if (safe != cv)
    {
        settings_set_lcd_contrast(safe);
    }
    lcd_set_contrast(safe);

    // カスタム文字登録
    lcd_init_arrow_chars();
    // キー初期化
    key_init();
    // 計算エンジン初期化
    init_rpn();
    // マクロ初期化
    macro_init();
    // レジューム復帰（有効時・正常時のみ）
    resume_try_restore_on_boot();
    // LCDクリア
    lcd_clear();
    // 初期画面表示
    refresh_display();
    // メニュー初期化
    menu_init();

    // 無操作タイマー初期化
    g_last_activity_ms = (uint32_t)to_ms_since_boot(get_absolute_time());

    // 電卓メインループ
    static bool s_prev_macro_playing = false;
    static bool s_prev_macro_recording = false;
    while (1)
    {
        uint32_t now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
        key_event_t ev;
        bool injected = false;
        if (macro_inject_next(&ev))
        {
            injected = true;
        }
        else
        {
            ev = key_poll();
        }
        bool need_refresh = false;
        if (ev.type != KEY_EVENT_NONE)
        {
            // 低電力中にキーが来たら高速クロックへ切り替え
            if (g_low_power)
                enter_high_speed_clock();

            // 最終操作時刻更新
            now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
            g_last_activity_ms = now_ms;
            // SHOW中はShiftのみ受け付け（ShiftでSHOW終了）。その他は無視。
            if (g_show_mode)
            {
                if (ev.type != KEY_EVENT_UP && ev.code == K_SHIFT)
                {
                    g_show_mode = false;
                    // SHOWを抜ける用途のため、シフト状態は解除しておく
                    key_set_shift_state(false);
                    need_refresh = true;
                }
            }
            else
            {
                // 定数UIが開いている間は、必要キーのみを ui_const で処理
                // ただしマクロ記録中であれば、UI内のキーも記録する
                if (const_ui_is_open())
                {
                    if (!injected)
                    {
                        macro_capture_event(ev); // 科学定数UI内の操作も記録
                    }
                    // UI専用ハンドラへ。ここでは他の処理（メニュー等）を行わない
                    bool handled = const_ui_handle_key(ev);
                    need_refresh = handled || need_refresh;
                }
                // マクロUIが開いているときはUI専用処理のみを行い他処理はブロック
                else if (macro_ui_is_open())
                {
                    if (!injected)
                    {
                        macro_capture_event(ev); // マクロUI内の操作も記録
                    }
                    bool handled = macro_ui_handle_key(ev);
                    need_refresh = handled || need_refresh;
                }
                else
                {
                    // 記録フック（注入/メニュー/マクロUI/定数UI中は除外）
                    if (!injected && !menu_is_open() && !macro_ui_is_open())
                    {
                        macro_capture_event(ev);
                    }

                    // メニュー開閉トグル（定数UI中はブロック）
                    if (ev.type != KEY_EVENT_UP && ev.code == K_MODE && !menu_is_open() && !macro_is_recording())
                    {
                        menu_open();
                        menu_render();
                    }
                    else if (menu_is_open())
                    {
                        // メニュー表示中のキー処理
                        bool handled = menu_handle_key(ev);
                        // メニューが閉じられた場合は通常画面を再描画
                        if (!menu_is_open())
                        {
                            need_refresh = true;
                        }
                        else if (handled)
                        {
                            // まだメニューが開いていて、何かしらの更新があればメニューを再描画
                            menu_render();
                        }
                    }
                    else
                    {
                        need_refresh = handle_key(ev) || need_refresh;
                    }
                }
            }
        }
        if (need_refresh)
            refresh_display();

        // マクロ再生/記録の状態変化でインジケータを更新
        bool now_playing = macro_is_playing();
        bool now_recording = macro_is_recording();
        if ((s_prev_macro_playing && !now_playing) || (s_prev_macro_recording && !now_recording))
        {
            // 他のUI表示中は通常画面を上書きしない
            if (!menu_is_open() && !macro_ui_is_open() && !const_ui_is_open())
            {
                refresh_display();
            }
        }
        s_prev_macro_playing = now_playing;
        s_prev_macro_recording = now_recording;

        // 設定された時間（0=無効）無操作なら自動OFF
        {
            uint32_t auto_off = auto_off_ms_setting();
            if (auto_off > 0 && (uint32_t)(now_ms - g_last_activity_ms) >= auto_off)
            {
                power_down_seq();
            }
        }

        // アイドル時の低電力遷移
        static const uint32_t idle_to_low_ms = IDLE_TO_LOW_MS; // 10s無操作で低速クロックへ
        if (!g_low_power && !macro_is_playing() && !macro_is_recording())
        {
            now_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
            if ((uint32_t)(now_ms - g_last_activity_ms) >= idle_to_low_ms)
            {
                enter_low_power_clock();
            }
        }
        sleep_ms(5);
    }
}