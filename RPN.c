#include "RPN.h"
#include "key.h"

// 科学定数（2グループ×10件）
typedef struct
{
    const char *symbol;
    const char *value_str;
    const char *name;
} sci_const_t;

// グループ1: 基本/熱統計/便宜
static const sci_const_t sci_group1[10] = {
    {"c", "2.99792458E8", "Light Speed"},
    {"h", "6.62607015E-34", "Planck h"},
    {"hbar", "1.0545718176461563912624280033022807447E-34", "Reduced h"},
    {"e", "1.602176634E-19", "Elem Charge"},
    {"me", "9.1093837139E-31", "Electron m"},
    {"k", "1.380649E-23", "Boltzmann"},
    {"NA", "6.02214076E23", "Avogadro"},
    {"R", "8.31446261815324", "Gas Const"},
    {"F", "9.64853321233100184E4", "Faraday"},
    {"g", "9.80665", "Std Gravity"}};
// グループ2: 電磁/量子/単位換算
static const sci_const_t sci_group2[10] = {
    {"mu0", "1.25663706127E-6", "Vacuum mu"},
    {"eps0", "8.8541878188E-12", "Vacuum eps"},
    {"Z0", "376.730313412", "Free Space Z"},
    {"alpha", "7.2973525643E-3", "Fine Struct"},
    {"sigma", "5.6703744191844294539709967318892308758E-8", "Stefan-Boltz"},
    {"Rinf", "10973731.568157", "Rydberg"},
    {"a0", "5.29177210544E-11", "Bohr Radius"},
    {"u", "1.66053906892E-27", "Atomic Mass"},
    {"mp", "1.67262192595E-27", "Proton m"},
    {"eV", "1.602176634E-19", "eV->J"}};

// #########################
//  スタック関連
// #########################
BID_UINT128 stack[4];
BID_UINT128 last_x;
// 変数メモリ VA..VF
static BID_UINT128 vars_mem[6];
static rpn_var_op_t pending_var_op = RPN_VAR_OP_NONE;

void stack_init()
{
    bid128_from_string(&last_x, "0");
    bid128_from_string(&stack[0], "0");
    bid128_from_string(&stack[1], "0");
    bid128_from_string(&stack[2], "0");
    bid128_from_string(&stack[3], "0");
    // 変数領域初期化
    for (int i = 0; i < 6; ++i)
        bid128_from_string(&vars_mem[i], "0");
    pending_var_op = RPN_VAR_OP_NONE;
}

void stack_push()
{
    stack[3] = stack[2];
    stack[2] = stack[1];
    stack[1] = stack[0];
}

void stack_pop()
{
    stack[0] = stack[1];
    stack[1] = stack[2];
    stack[2] = stack[3];
}

void stack_swap()
{
    BID_UINT128 tmp = stack[0];
    stack[0] = stack[1];
    stack[1] = tmp;
}

void stack_roll_up()
{
    BID_UINT128 tmp = stack[3];
    stack[3] = stack[2];
    stack[2] = stack[1];
    stack[1] = stack[0];
    stack[0] = tmp;
}

void stack_roll_down()
{
    BID_UINT128 tmp = stack[0];
    stack[0] = stack[1];
    stack[1] = stack[2];
    stack[2] = stack[3];
    stack[3] = tmp;
}

// #########################
// ヘルパー関数
// #########################
input_state_t input_state;
static _IDEC_flags last_exceptions = BID_EXACT_STATUS; // 直近演算の例外フラグを保持
flag_state_t flag_state;

// フラグをクリア
void clear_flag_state()
{
    flag_state.push_flag = false;
}
// 入力状態をクリア
void clear_input_state()
{
    for (int i = 0; i < MAX_INPUTVAL_LENGTH; i++)
    {
        input_state.input_str[i] = '\0';
    }
    input_state.input_len = 0;
    input_state.input_sign = false;
    input_state.dot_pos = -1;
    input_state.exp_pos = -1;
    input_state.exp_sign = false;
}

// 数字キーが押されたときの処理
void handle_digit(char digit)
{
    if (flag_state.push_flag)
    {
        stack_push();
        clear_input_state();
        flag_state.push_flag = false;
    }
    if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
    {
        input_state.input_str[input_state.input_len] = digit;
        input_state.input_len++;
    }
    bid128_from_string(&stack[0], input_state.input_str);
}

// 演算の後の処理
void after_operation()
{
    // 直前の演算で立った例外フラグを保存してから全クリア
    last_exceptions = _IDEC_glbflags;
    _IDEC_glbflags = BID_EXACT_STATUS;

    // 結果が±0E±n になっている場合は +0E+0 に正規化する（表示や後続計算の一貫性のため）
    int is_zero = 0;
    __bid128_isZero(&is_zero, &stack[0]);
    if (is_zero)
    {
        bid128_from_string(&stack[0], "0");
    }

    // NaN/Inf のときは次の入力で自動 push しない（特殊値がYに残らないようにする）
    int is_inf = 0, is_nan = 0;
    __bid128_isInf(&is_inf, &stack[0]);
    __bid128_isNaN(&is_nan, &stack[0]);

    clear_input_state();
    flag_state.push_flag = !(is_inf || is_nan);
    // 演算終了時はシフト状態を解除
    key_set_shift_state(false);
}

// 直近演算の例外フラグを返す（BID_*_EXCEPTION の OR）。
// 取得のみでフラグは維持される（必要なら利用側でゼロクリア）。
_IDEC_flags rpn_get_last_exceptions()
{
    return last_exceptions;
}

// #########################
// 設定読み書き
// #########################
#include "settings.h"
init_state_t init_state;
static void load_settings()
{
    settings_init();
    settings_load_into(&init_state);
}
static void save_settings() { settings_save_if_dirty(); }

// #########################
// BID128->文字列変換
// #########################
// ---- ヘルパ関数（このファイル内限定） ----
static int rpn_digits_count_int(int v)
{
    int a = (v < 0) ? -v : v;
    int c = 1;
    while (a >= 10)
    {
        a /= 10;
        ++c;
    }
    return c;
}

// 最近接・偶数丸め（銀行家の丸め）。digits は数値の連続桁列（'0'..'9'）。
// len は現在桁数、keep は保持したい桁数（keep 桁目以降を丸める）。
// 返り値は新しい桁数（キャリー発生で +1 の可能性）。
static int rpn_round_bankers(char *digits, int len, int keep)
{
    if (keep >= len || keep < 0)
        return len; // 丸め不要
    int next = digits[keep] - '0';
    int i;
    bool any_after = false;
    for (i = keep + 1; i < len; ++i)
        if (digits[i] != '0')
        {
            any_after = true;
            break;
        }
    bool round_up = false;
    if (next > 5)
        round_up = true;
    else if (next < 5)
        round_up = false;
    else
    { // 5
        if (any_after)
            round_up = true;
        else
        {
            int last = keep - 1;
            int last_digit = (last >= 0) ? (digits[last] - '0') : 0;
            round_up = (last_digit % 2) == 1; // 奇数なら繰上げ
        }
    }

    int new_len = keep;
    if (round_up)
    {
        i = keep - 1;
        for (; i >= 0; --i)
        {
            int d = (digits[i] - '0') + 1;
            if (d == 10)
                digits[i] = '0';
            else
            {
                digits[i] = (char)('0' + d);
                break;
            }
        }
        if (i < 0)
        {
            // 先頭にキャリーを追加（容量に注意: 呼び出し側で余裕あり）
            int j;
            for (j = keep; j > 0; --j)
                digits[j] = digits[j - 1];
            digits[0] = '1';
            new_len = keep + 1;
        }
    }
    digits[new_len] = '\0';
    return new_len;
}

void bid128_to_str(BID_UINT128 x, char *buf, int bufsize)
{
    // 安全ガード
    if (!buf || bufsize <= 0)
        return;
    buf[0] = '\0';

    // ライブラリ出力を一旦受ける（最大34桁+符号+"E"+指数。十分に余裕を持つ）
    char raw[160];
    bid128_to_string(raw, &x);

    // 解析: 符号, 特殊値, 仮数digits, 指数
    const char *p = raw;
    bool neg = false;
    if (*p == '+' || *p == '-')
    {
        neg = (*p == '-');
        ++p;
    }

    // 特殊値（Inf/NaN）
    if (strncmp(p, "Inf", 3) == 0)
    {
        const char *s = neg ? "-Inf" : "Inf";
        int n = (int)strlen(s);
        int maxc = bufsize - 1;
        if (maxc <= 0)
        {
            buf[0] = '\0';
            return;
        }
        if (n > maxc)
            n = maxc;
        memcpy(buf, s, (size_t)n);
        buf[n] = '\0';
        return;
    }
    if (strncmp(p, "NaN", 3) == 0)
    {
        const char *s = "NaN";
        int n = (int)strlen(s);
        int maxc = bufsize - 1;
        if (maxc <= 0)
        {
            buf[0] = '\0';
            return;
        }
        if (n > maxc)
            n = maxc;
        memcpy(buf, s, (size_t)n);
        buf[n] = '\0';
        return;
    }

    // p は digits の先頭。'E' までが仮数。
    char mant[64];
    int mlen = 0;
    while (*p && *p != 'E' && mlen < (int)sizeof(mant) - 1)
    {
        if (*p >= '0' && *p <= '9')
            mant[mlen++] = *p;
        ++p;
    }
    mant[mlen] = '\0';

    // 指数
    int exp10 = 0;
    if (*p == 'E')
    {
        ++p;
        bool eneg = false;
        if (*p == '+' || *p == '-')
        {
            eneg = (*p == '-');
            ++p;
        }
        while (*p >= '0' && *p <= '9')
        {
            exp10 = exp10 * 10 + (*p - '0');
            ++p;
        }
        if (eneg)
            exp10 = -exp10;
    }

    // 出力用の上限
    int max_chars = bufsize - 1;
    if (max_chars <= 0)
    {
        buf[0] = '\0';
        return;
    }

    int out_i = 0; // 出力位置

#define PUSH_CH(C)              \
    do                          \
    {                           \
        if (out_i < max_chars)  \
            buf[out_i++] = (C); \
    } while (0)
#define PUSH_STR(S)                      \
    do                                   \
    {                                    \
        const char *_s = (S);            \
        while (*_s && out_i < max_chars) \
            buf[out_i++] = *_s++;        \
    } while (0)

    // 表示モードごとの整形
    disp_mode_t mode = init_state.disp_mode;

    if (mode == DISP_MODE_SCIENTIFIC)
    {
        int esci = exp10 + (mlen > 0 ? (mlen - 1) : 0);
        int exp_digits = rpn_digits_count_int(esci);
        int base = 1 + 1 + 1 + exp_digits; // 先頭桁 + 'E' + 符号 + 指数桁
        int avail = max_chars - (neg ? 1 : 0);
        int sig = 1; // 実際に丸めに使う有効桁（ライブラリ出力桁数で上限）
        int cap = 1; // 画面に表示可能な有効桁（ゼロ埋めの目標）
        if (avail > base)
        {
            cap = 1 + (avail - base - 1); // '.' 分を引く
            sig = cap;
            if (sig > mlen)
                sig = mlen;
        }
        char tmp[64];
        memcpy(tmp, mant, (size_t)mlen + 1);
        int keep = sig;
        int newlen = rpn_round_bankers(tmp, mlen, keep);
        if (newlen > keep)
        {
            esci += 1;
            int exp_digits2 = rpn_digits_count_int(esci);
            if (exp_digits2 > exp_digits)
            {
                int base2 = 1 + 1 + 1 + exp_digits2;
                int avail2 = max_chars - (neg ? 1 : 0);
                int sig2 = 1;
                int cap2 = 1;
                if (avail2 > base2)
                {
                    cap2 = 1 + (avail2 - base2 - 1);
                    sig2 = cap2;
                    if (sig2 > newlen)
                        sig2 = newlen;
                }
                if (sig2 < newlen)
                {
                    keep = sig2;
                    newlen = rpn_round_bankers(tmp, newlen, keep);
                }
                // ゼロ埋めの目標桁は最新のcap2を採用
                cap = (avail2 > base2) ? cap2 : 1;
            }
        }

        if (neg)
            PUSH_CH('-');
        PUSH_CH(tmp[0]);
        {
            int start = 1;
            int available_frac = (newlen > 1) ? (newlen - 1) : 0;
            if (init_state.zero_mode == ZERO_MODE_TRIM)
            {
                int end = newlen;
                while (end > start && tmp[end - 1] == '0')
                    --end;
                if (end > start && out_i < max_chars)
                {
                    PUSH_CH('.');
                    for (int i = start; i < end && out_i < max_chars; ++i)
                        PUSH_CH(tmp[i]);
                }
            }
            else // ZERO_MODE_PAD: 表示可能桁までゼロ埋め
            {
                int desired_frac = (cap > 1) ? (cap - 1) : 0;
                if (desired_frac > 0 && out_i < max_chars)
                {
                    PUSH_CH('.');
                    int shown = 0;
                    int take = (available_frac < desired_frac) ? available_frac : desired_frac;
                    for (int i = 0; i < take && out_i < max_chars; ++i)
                    {
                        PUSH_CH(tmp[start + i]);
                        shown++;
                    }
                    while (shown < desired_frac && out_i < max_chars)
                    {
                        PUSH_CH('0');
                        shown++;
                    }
                }
            }
        }
        PUSH_CH('E');
        if (esci < 0)
        {
            PUSH_CH('-');
            esci = -esci;
        }
        else
        {
            PUSH_CH('+');
        }
        // 指数数字
        char expbuf[16];
        int ei = 0;
        do
        {
            expbuf[ei++] = (char)('0' + (esci % 10));
            esci /= 10;
        } while (esci && ei < (int)sizeof(expbuf));
        int i;
        for (i = ei - 1; i >= 0 && out_i < max_chars; --i)
            PUSH_CH(expbuf[i]);
        buf[out_i] = '\0';
        return;
    }

    if (mode == DISP_MODE_ENGINEERING)
    {
        // 工学表記: 指数は3の倍数。小数点位置 dec_pos = mlen + exp10
        int dec_pos = mlen + exp10;
        int r = dec_pos % 3;
        if (r < 0)
            r += 3; // 正の剰余
        int digits_before = (r == 0) ? 3 : r;
        int eeng = dec_pos - digits_before; // 常に3の倍数

        // 一旦、今の指数桁数で入るだけ有効桁を詰める
        int exp_digits = rpn_digits_count_int(eeng);
        int avail = max_chars - (neg ? 1 : 0);
        int base = digits_before + 1 /*'E'*/ + 1 /*exp sign*/ + exp_digits; // '.' は後で
        int sig = digits_before;                                            // 丸め用
        int cap = digits_before;                                            // 表示可能有効桁（ゼロ埋め目標）
        if (avail > base)
        {
            int extra = avail - base - 1; // '.' の分
            if (extra > 0)
                cap = digits_before + extra;
            sig = cap;
            if (sig > mlen)
                sig = mlen;
        }

        char tmp[64];
        memcpy(tmp, mant, (size_t)mlen + 1);
        int keep = sig;
        int newlen = rpn_round_bankers(tmp, mlen, keep);
        if (newlen > keep)
        {
            // 丸めで桁上がり → 値が10倍になったので dec_pos を +1 して再計算
            dec_pos += 1;
            r = dec_pos % 3;
            if (r < 0)
                r += 3;
            digits_before = (r == 0) ? 3 : r;
            eeng = dec_pos - digits_before;
            // 指数桁数が変化したら再配分
            int exp_digits2 = rpn_digits_count_int(eeng);
            if (exp_digits2 != exp_digits)
            {
                exp_digits = exp_digits2;
                avail = max_chars - (neg ? 1 : 0);
                base = digits_before + 1 + 1 + exp_digits;
                int sig2 = digits_before;
                int cap2 = digits_before;
                if (avail > base)
                {
                    int extra = avail - base - 1;
                    if (extra > 0)
                        cap2 = digits_before + extra;
                    sig2 = cap2;
                    if (sig2 > newlen)
                        sig2 = newlen;
                }
                if (sig2 < newlen)
                {
                    keep = sig2;
                    newlen = rpn_round_bankers(tmp, newlen, keep);
                }
                cap = (avail > base) ? cap2 : digits_before;
            }
        }

        if (neg)
            PUSH_CH('-');
        int i;
        // 整数部は必ず digits_before 桁にする（不足分は0埋め）
        for (i = 0; i < digits_before && out_i < max_chars; ++i)
        {
            char d = (i < newlen) ? tmp[i] : '0';
            PUSH_CH(d);
        }
        // 小数部は digits_before 以降の残りを出力
        if (out_i < max_chars)
        {
            int start = digits_before;
            int available_frac = (newlen > digits_before) ? (newlen - digits_before) : 0;
            if (init_state.zero_mode == ZERO_MODE_TRIM)
            {
                int end = newlen;
                while (end > start && tmp[end - 1] == '0')
                    --end;
                if (end > start && out_i < max_chars)
                {
                    PUSH_CH('.');
                    for (i = start; i < end && out_i < max_chars; ++i)
                        PUSH_CH(tmp[i]);
                }
            }
            else // ZERO_MODE_PAD
            {
                int desired_frac = (cap > digits_before) ? (cap - digits_before) : 0;
                if (desired_frac > 0 && out_i < max_chars)
                {
                    PUSH_CH('.');
                    int shown = 0;
                    int take = (available_frac < desired_frac) ? available_frac : desired_frac;
                    for (i = 0; i < take && out_i < max_chars; ++i)
                    {
                        PUSH_CH(tmp[start + i]);
                        shown++;
                    }
                    while (shown < desired_frac && out_i < max_chars)
                    {
                        PUSH_CH('0');
                        shown++;
                    }
                }
            }
        }
        PUSH_CH('E');
        if (eeng < 0)
        {
            PUSH_CH('-');
            eeng = -eeng;
        }
        else
        {
            PUSH_CH('+');
        }
        char expbuf[16];
        int ei = 0;
        do
        {
            expbuf[ei++] = (char)('0' + (eeng % 10));
            eeng /= 10;
        } while (eeng && ei < (int)sizeof(expbuf));
        for (i = ei - 1; i >= 0 && out_i < max_chars; --i)
            PUSH_CH(expbuf[i]);
        buf[out_i] = '\0';
        return;
    }

    // NORMAL（固定小数点）
    int dec_pos = mlen + exp10;

    int int_len = 0;
    if (dec_pos > 0)
        int_len = dec_pos;
    else
        int_len = 1;
    if ((neg ? 1 : 0) + int_len > max_chars)
    {
        // 科学表記へフォールバック
        disp_mode_t old = init_state.disp_mode;
        init_state.disp_mode = DISP_MODE_SCIENTIFIC;
        bid128_to_str(x, buf, bufsize);
        init_state.disp_mode = old;
        return;
    }

    int frac_orig_len = 0;
    if (dec_pos >= mlen)
    {
        frac_orig_len = 0;
    }
    else if (dec_pos <= 0)
    {
        frac_orig_len = -dec_pos + mlen;
    }
    else
    {
        frac_orig_len = mlen - dec_pos;
    }

    int avail_after_int = max_chars - (neg ? 1 : 0) - int_len;
    // 画面に表示可能な小数部の上限（パディング目標）。元の小数桁数で制限しない。
    int cap_frac = 0;
    if (avail_after_int > 0)
    {
        cap_frac = avail_after_int - 1; // '.' の分を引く
        if (cap_frac < 0)
            cap_frac = 0;
    }

    // 極小値フォールバック: 小数点以下に先頭の有効数字が到達しない場合は科学表記へ
    // dec_pos <= 0 のとき、先頭の有効数字は小数点の右に (-dec_pos) 桁の0の後に現れる。
    // 表示可能な小数桁 cap_frac がその手前までしかない（cap_frac <= -dec_pos）場合、
    // 有効数字が1桁も表示されないため科学表記へ切り替える。
    if (dec_pos <= 0)
    {
        int leading_zeros = -dec_pos;
        if (cap_frac <= leading_zeros)
        {
            disp_mode_t old = init_state.disp_mode;
            init_state.disp_mode = DISP_MODE_SCIENTIFIC;
            bid128_to_str(x, buf, bufsize);
            init_state.disp_mode = old;
            return;
        }
    }

    char intbuf[80];
    int intbuf_len = 0;
    if (dec_pos > 0)
    {
        int i;
        for (i = 0; i < dec_pos; ++i)
        {
            char d = (i < mlen) ? mant[i] : '0';
            if (intbuf_len < (int)sizeof(intbuf) - 1)
                intbuf[intbuf_len++] = d;
        }
    }
    else
    {
        intbuf[intbuf_len++] = '0';
    }

    char frac_full[96];
    int ffull = 0;
    if (dec_pos < 0)
    {
        int i;
        for (i = 0; i < -dec_pos && ffull < (int)sizeof(frac_full) - 1; ++i)
            frac_full[ffull++] = '0';
        for (i = 0; i < mlen && ffull < (int)sizeof(frac_full) - 1; ++i)
            frac_full[ffull++] = mant[i];
    }
    else if (dec_pos < mlen)
    {
        int i;
        for (i = dec_pos; i < mlen && ffull < (int)sizeof(frac_full) - 1; ++i)
            frac_full[ffull++] = mant[i];
    }
    else
    {
        // 小数部無し
    }
    frac_full[ffull] = '\0';

    // 丸めで保持する小数桁は「表示可能上限」と「元の小数桁」の小さい方
    int keep_frac = (cap_frac < ffull) ? cap_frac : ffull;
    if (keep_frac < ffull)
    {
        char work[128];
        int wlen = 0;
        int i;
        for (i = 0; i < intbuf_len && wlen < (int)sizeof(work) - 1; ++i)
            work[wlen++] = intbuf[i];
        for (i = 0; i < ffull && wlen < (int)sizeof(work) - 1; ++i)
            work[wlen++] = frac_full[i];
        work[wlen] = '\0';
        int keep_total = intbuf_len + keep_frac;
        int new_wlen = rpn_round_bankers(work, wlen, keep_total);
        int new_int_len = (new_wlen > keep_total) ? (intbuf_len + 1) : intbuf_len;
        if ((neg ? 1 : 0) + new_int_len > max_chars)
        {
            disp_mode_t old = init_state.disp_mode;
            init_state.disp_mode = DISP_MODE_SCIENTIFIC;
            bid128_to_str(x, buf, bufsize);
            init_state.disp_mode = old;
            return;
        }
        intbuf_len = new_int_len;
        for (i = 0; i < intbuf_len; ++i)
            intbuf[i] = work[i];
        int new_frac_len = new_wlen - intbuf_len;
        // 丸め後に整数部が増えた可能性があるため、表示可能な小数桁(cap)を再計算
        int avail_after_int2 = max_chars - (neg ? 1 : 0) - intbuf_len;
        int cap_frac2 = (avail_after_int2 > 0) ? (avail_after_int2 - 1) : 0;
        if (cap_frac2 < 0)
            cap_frac2 = 0;
        if (new_frac_len > cap_frac2)
            new_frac_len = cap_frac2;
        if (init_state.zero_mode == ZERO_MODE_TRIM)
            while (new_frac_len > 0 && work[intbuf_len + new_frac_len - 1] == '0')
                new_frac_len--;

        if (neg)
            PUSH_CH('-');
        for (i = 0; i < intbuf_len && out_i < max_chars; ++i)
            PUSH_CH(intbuf[i]);
        if (out_i < max_chars)
        {
            if (init_state.zero_mode == ZERO_MODE_TRIM)
            {
                if (new_frac_len > 0)
                {
                    PUSH_CH('.');
                    for (i = 0; i < new_frac_len && out_i < max_chars; ++i)
                        PUSH_CH(work[intbuf_len + i]);
                }
            }
            else // ZERO_MODE_PAD: ちょうどmax_frac桁までゼロ埋め
            {
                // PAD: 表示可能上限までゼロ埋め
                if (cap_frac2 > 0)
                {
                    PUSH_CH('.');
                    int shown = 0;
                    int take = (new_frac_len < cap_frac2) ? new_frac_len : cap_frac2;
                    for (i = 0; i < take && out_i < max_chars; ++i)
                    {
                        PUSH_CH(work[intbuf_len + i]);
                        shown++;
                    }
                    while (shown < cap_frac2 && out_i < max_chars)
                    {
                        PUSH_CH('0');
                        shown++;
                    }
                }
            }
        }
        buf[out_i] = '\0';
        return;
    }
    else
    {
        int new_frac_len = ffull;
        if (init_state.zero_mode == ZERO_MODE_TRIM)
            while (new_frac_len > 0 && frac_full[new_frac_len - 1] == '0')
                new_frac_len--;
        if (neg)
            PUSH_CH('-');
        int i;
        for (i = 0; i < intbuf_len && out_i < max_chars; ++i)
            PUSH_CH(intbuf[i]);
        if (out_i < max_chars)
        {
            if (init_state.zero_mode == ZERO_MODE_TRIM)
            {
                if (new_frac_len > 0)
                {
                    PUSH_CH('.');
                    for (i = 0; i < new_frac_len && out_i < max_chars; ++i)
                        PUSH_CH(frac_full[i]);
                }
            }
            else // ZERO_MODE_PAD
            {
                // 現在の整数部長に基づく表示可能小数桁を算出
                int avail_after_int2 = max_chars - (neg ? 1 : 0) - intbuf_len;
                int cap_frac2 = (avail_after_int2 > 0) ? (avail_after_int2 - 1) : 0;
                if (cap_frac2 < 0)
                    cap_frac2 = 0;
                if (cap_frac2 > 0)
                {
                    PUSH_CH('.');
                    int shown = 0;
                    int take = (new_frac_len < cap_frac2) ? new_frac_len : cap_frac2;
                    for (i = 0; i < take && out_i < max_chars; ++i)
                    {
                        PUSH_CH(frac_full[i]);
                        shown++;
                    }
                    while (shown < cap_frac2 && out_i < max_chars)
                    {
                        PUSH_CH('0');
                        shown++;
                    }
                }
            }
        }
        buf[out_i] = '\0';
        return;
    }
}

// #########################
//  RPN電卓本体
// #########################
void init_rpn()
{
    _IDEC_glbflags = BID_EXACT_STATUS;
    _IDEC_glbround = BID_ROUNDING_TO_NEAREST;
    stack_init();
    clear_flag_state();
    clear_input_state();
    load_settings();
}

// #########################
//  RPNコア公開API
// #########################

// スタック参照
BID_UINT128 rpn_stack_x() { return stack[0]; }
BID_UINT128 rpn_stack_y() { return stack[1]; }
BID_UINT128 rpn_stack_z() { return stack[2]; }
BID_UINT128 rpn_stack_t() { return stack[3]; }
// Xレジスタを直接設定（入力状態はクリア）
void rpn_set_x(BID_UINT128 x)
{
    // LAST X は上書き前のXを保存
    last_x = stack[0];
    stack[0] = x;
    clear_input_state();
}

void rpn_var_set_pending_op(rpn_var_op_t op)
{
    pending_var_op = op;
}

rpn_var_op_t rpn_var_get_pending_op(void)
{
    return pending_var_op;
}

bool rpn_var_apply_slot(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= 6)
        return false;
    if (pending_var_op == RPN_VAR_OP_NONE)
        return false;
    switch (pending_var_op)
    {
    case RPN_VAR_OP_ST:
        vars_mem[slot_idx] = stack[0];
        break;
    case RPN_VAR_OP_LD:
    {
        // pi/e と同じ挙動: 必要時のみpush → 入力クリア → Xへ設定 → 次の数値入力でpushさせる
        if (flag_state.push_flag)
        {
            stack_push();
        }
        clear_input_state();
        stack[0] = vars_mem[slot_idx];
        flag_state.push_flag = true;
        break;
    }
    case RPN_VAR_OP_CLR:
    {
        BID_UINT128 zero;
        bid128_from_string(&zero, "0");
        vars_mem[slot_idx] = zero;
        break;
    }
    default:
        return false;
    }
    pending_var_op = RPN_VAR_OP_NONE;
    return true;
}

char rpn_var_indicator_char(void)
{
    switch (pending_var_op)
    {
    case RPN_VAR_OP_ST:
        return 'S';
    case RPN_VAR_OP_LD:
        return 'L';
    case RPN_VAR_OP_CLR:
        return 'C';
    default:
        return '\0';
    }
}

static const sci_const_t *pick_group(int group)
{
    if (group == 1)
        return sci_group1;
    if (group == 2)
        return sci_group2;
    return NULL;
}

bool rpn_const_apply(int group, int index)
{
    const sci_const_t *grp = pick_group(group);
    if (!grp)
        return false;
    if (index < 0 || index >= 10)
        return false;
    // rpn_input_pi/e と同様のスタック操作に合わせる:
    // 必要時のみpush → 入力クリア → Xへ設定 → 次の数値入力でpushさせる
    BID_UINT128 v;
    // APIがchar*を要求するため、値文字列をローカルの可変バッファへコピー
    char vbuf[48];
    strncpy(vbuf, grp[index].value_str, sizeof(vbuf) - 1);
    vbuf[sizeof(vbuf) - 1] = '\0';
    bid128_from_string(&v, vbuf);
    if (flag_state.push_flag)
    {
        stack_push();
    }
    clear_input_state();
    stack[0] = v;
    flag_state.push_flag = true;
    return true;
}

const char *rpn_const_symbol(int group, int index)
{
    const sci_const_t *grp = pick_group(group);
    if (!grp)
        return NULL;
    if (index < 0 || index >= 10)
        return NULL;
    return grp[index].symbol;
}

const char *rpn_const_name(int group, int index)
{
    const sci_const_t *grp = pick_group(group);
    if (!grp)
        return NULL;
    if (index < 0 || index >= 10)
        return NULL;
    return grp[index].name;
}

int rpn_const_group_size(int group)
{
    (void)group;
    return 10;
}

// 入力ヘルパ: 現在の入力文字列が数値として完結しているか
static bool input_ends_with_digit()
{
    if (input_state.input_len <= 0)
        return false;
    char c = input_state.input_str[input_state.input_len - 1];
    return (c >= '0' && c <= '9');
}

// 入力→X反映
static void update_x_from_input_if_valid()
{
    if (input_state.input_len == 0)
        return; // 空なら触らない
    if (!input_ends_with_digit())
        return; // 不完全な入力は反映しない
    bid128_from_string(&stack[0], input_state.input_str);
}

void rpn_input_clear()
{
    clear_input_state();
}

void rpn_input_append_digit(char digit)
{
    if (flag_state.push_flag)
    {
        stack_push();
        clear_input_state();
        flag_state.push_flag = false;
    }
    if (input_state.input_len == 0 && input_state.input_sign)
    {
        // 先頭に'-'があるケースに対応
    }
    if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
    {
        input_state.input_str[input_state.input_len++] = digit;
        input_state.input_str[input_state.input_len] = '\0';
    }
    update_x_from_input_if_valid();
}

void rpn_input_dot()
{
    if (flag_state.push_flag)
    {
        stack_push();
        clear_input_state();
        flag_state.push_flag = false;
    }
    if (input_state.dot_pos >= 0)
        return; // 重複禁止
    if (input_state.input_len == 0)
    {
        // "0." から開始
        if (input_state.input_len < MAX_INPUTVAL_LENGTH - 2)
        {
            input_state.input_str[input_state.input_len++] = '0';
            input_state.input_str[input_state.input_len++] = '.';
            input_state.input_str[input_state.input_len] = '\0';
            input_state.dot_pos = 1;
        }
    }
    else
    {
        if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
        {
            input_state.input_str[input_state.input_len++] = '.';
            input_state.input_str[input_state.input_len] = '\0';
            input_state.dot_pos = (int8_t)(input_state.input_len - 1);
        }
    }
}

void rpn_input_exp()
{
    if (flag_state.push_flag)
    {
        stack_push();
        clear_input_state();
        flag_state.push_flag = false;
    }
    if (input_state.exp_pos >= 0)
        return; // 重複禁止
    if (input_state.input_len == 0)
    {
        // 先頭から指数は不可 → 先に"1E"相当とするか、無視
        return;
    }
    if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
    {
        input_state.input_str[input_state.input_len++] = 'E';
        input_state.input_str[input_state.input_len] = '\0';
        input_state.exp_pos = (int8_t)(input_state.input_len - 1);
    }
}

void rpn_input_toggle_sign()
{
    // 入力中は入力文字列の符号を操作
    if (rpn_is_input_active())
    {
        // 指数部の符号切り替え優先
        if (input_state.exp_pos >= 0)
        {
            int pos = input_state.exp_pos + 1;
            if (pos < input_state.input_len)
            {
                if (input_state.input_str[pos] == '+')
                    input_state.input_str[pos] = '-';
                else if (input_state.input_str[pos] == '-')
                    input_state.input_str[pos] = '+';
                else
                {
                    // 挿入（要シフト）
                    if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
                    {
                        for (int i = input_state.input_len; i > pos; --i)
                            input_state.input_str[i] = input_state.input_str[i - 1];
                        input_state.input_str[pos] = '-';
                        input_state.input_len++;
                        input_state.input_str[input_state.input_len] = '\0';
                    }
                }
            }
            else
            {
                // Eの直後 → 符号追加
                if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
                {
                    input_state.input_str[input_state.input_len++] = '-';
                    input_state.input_str[input_state.input_len] = '\0';
                }
            }
            update_x_from_input_if_valid();
            return;
        }

        // 仮数部の符号をトグル
        if (input_state.input_len > 0 && (input_state.input_str[0] == '-' || input_state.input_str[0] == '+'))
        {
            input_state.input_str[0] = (input_state.input_str[0] == '-') ? '+' : '-';
        }
        else
        {
            if (input_state.input_len < MAX_INPUTVAL_LENGTH - 1)
            {
                // 先頭に'-'を挿入
                for (int i = input_state.input_len; i > 0; --i)
                    input_state.input_str[i] = input_state.input_str[i - 1];
                input_state.input_str[0] = '-';
                input_state.input_len++;
                input_state.input_str[input_state.input_len] = '\0';
            }
        }
        update_x_from_input_if_valid();
    }
    else
    {
        // 入力確定後はXレジスタの符号を反転
        last_x = stack[0];
        __bid128_negate(&stack[0], &stack[0]);
        after_operation();
    }
}

// 1文字削除（指数記号や符号も含む）
void rpn_input_backspace()
{
    if (input_state.input_len <= 0)
        return;
    char removed = input_state.input_str[input_state.input_len - 1];
    input_state.input_len--;
    input_state.input_str[input_state.input_len] = '\0';
    if (removed == '.')
        input_state.dot_pos = -1;
    if (removed == 'E')
        input_state.exp_pos = -1;
    // Eの直後の符号を消した場合の整合性は単純化のため放置（次の入力で上書き）
    if (input_state.input_len == 1 && (input_state.input_str[0] == '+' || input_state.input_str[0] == '-'))
    {
        // 単独符号は無効とみなしてクリア
        clear_input_state();
        bid128_from_string(&stack[0], "0");
        return;
    }
    if (input_state.input_len == 0)
    {
        // 空になったらXを0に
        bid128_from_string(&stack[0], "0");
        return;
    }
    update_x_from_input_if_valid();
}

void rpn_clear_x()
{
    clear_input_state();
    bid128_from_string(&stack[0], "0");
    // 次の入力で上書き開始
    flag_state.push_flag = false;
}

// 入力だけを確定してXに反映（pushしない）
void rpn_commit_input_without_push()
{
    if (input_state.input_len > 0)
    {
        update_x_from_input_if_valid();
    }
    // 入力は確定済みとみなすが、次の数字入力で自動pushが起きないようにする
    clear_input_state();
    flag_state.push_flag = false;
}

void rpn_enter()
{
    // 入力確定してプッシュ
    if (input_state.input_len > 0)
        update_x_from_input_if_valid();
    stack_push();
    // 次の入力は新規値として開始（余計な自動pushはさせない）
    clear_input_state();
    flag_state.push_flag = false;
}

void rpn_swap()
{
    stack_swap();
    clear_input_state();
    flag_state.push_flag = true;
}
void rpn_roll_up()
{
    stack_roll_up();
    clear_input_state();
    flag_state.push_flag = true;
}
void rpn_roll_down()
{
    stack_roll_down();
    clear_input_state();
    flag_state.push_flag = true;
}

void rpn_add()
{
    last_x = stack[0];
    BID_UINT128 res;
    __bid128_add(&res, &stack[1], &stack[0]);
    stack_pop();
    stack[0] = res;
    after_operation();
}
void rpn_sub()
{
    last_x = stack[0];
    BID_UINT128 res;
    __bid128_sub(&res, &stack[1], &stack[0]); // y - x
    stack_pop();
    stack[0] = res;
    after_operation();
}
void rpn_mul()
{
    last_x = stack[0];
    BID_UINT128 res;
    __bid128_mul(&res, &stack[1], &stack[0]);
    stack_pop();
    stack[0] = res;
    after_operation();
}
void rpn_div()
{
    last_x = stack[0];
    BID_UINT128 res;
    __bid128_div(&res, &stack[1], &stack[0]); // y / x
    stack_pop();
    stack[0] = res;
    after_operation();
}

// 単項演算
void rpn_sqrt()
{
    last_x = stack[0];
    __bid128_sqrt(&stack[0], &stack[0]);
    after_operation();
}
void rpn_rev()
{
    // 1 / x
    last_x = stack[0];
    BID_UINT128 one, res;
    bid128_from_string(&one, "1");
    __bid128_div(&res, &one, &stack[0]);
    stack[0] = res;
    after_operation();
}
void rpn_pow2()
{
    last_x = stack[0];
    BID_UINT128 t;
    __bid128_mul(&t, &stack[0], &stack[0]);
    stack[0] = t;
    after_operation();
}
void rpn_pow()
{
    // y^x
    last_x = stack[0];
    BID_UINT128 res;
    __bid128_pow(&res, &stack[1], &stack[0]);
    stack_pop();
    stack[0] = res;
    after_operation();
}
void rpn_nth_root()
{
    // y√x = x^(1/y)
    last_x = stack[0];
    BID_UINT128 one, inv_y, res;
    bid128_from_string(&one, "1");
    __bid128_div(&inv_y, &one, &stack[1]); // 1/Y
    __bid128_pow(&res, &stack[0], &inv_y); // X^(1/Y)
    stack_pop();
    stack[0] = res;
    after_operation();
}
void rpn_log()
{
    // log10(x)
    last_x = stack[0];
    __bid128_log10(&stack[0], &stack[0]);
    after_operation();
}
void rpn_ln()
{
    // ln(x)
    last_x = stack[0];
    __bid128_log(&stack[0], &stack[0]);
    after_operation();
}
// 角度→ラジアン変換（DEG/GRAD→RAD。RADはそのまま）
static void rpn_convert_angle_to_rad(BID_UINT128 *x)
{
    if (init_state.angle_mode == ANGLE_MODE_DEG)
    {
        BID_UINT128 k; // pi/180
        bid128_from_string(&k, "0.017453292519943295769236907684886127134");
        __bid128_mul(x, x, &k);
    }
    else if (init_state.angle_mode == ANGLE_MODE_GRAD)
    {
        BID_UINT128 k; // pi/200
        bid128_from_string(&k, "0.015707963267948966192313216916397514421");
        __bid128_mul(x, x, &k);
    }
}

void rpn_sin()
{
    // 角度モードに応じて入力xをラジアンへ変換しsin
    last_x = stack[0];
    BID_UINT128 x = stack[0];
    BID_UINT128 res;
    rpn_convert_angle_to_rad(&x);
    __bid128_sin(&res, &x);
    stack[0] = res;
    after_operation();
}
void rpn_cos()
{
    last_x = stack[0];
    BID_UINT128 x = stack[0];
    BID_UINT128 res;
    rpn_convert_angle_to_rad(&x);
    __bid128_cos(&res, &x);
    stack[0] = res;
    after_operation();
}
void rpn_tan()
{
    last_x = stack[0];
    BID_UINT128 x = stack[0];
    BID_UINT128 res;
    rpn_convert_angle_to_rad(&x);
    __bid128_tan(&res, &x);
    stack[0] = res;
    after_operation();
}

// 追加単項・二項演算
void rpn_cube()
{
    last_x = stack[0];
    BID_UINT128 t, res;
    __bid128_mul(&t, &stack[0], &stack[0]);
    __bid128_mul(&res, &t, &stack[0]);
    stack[0] = res;
    after_operation();
}

void rpn_cbrt()
{
    last_x = stack[0];
    __bid128_cbrt(&stack[0], &stack[0]);
    after_operation();
}

void rpn_exp()
{
    last_x = stack[0];
    __bid128_exp(&stack[0], &stack[0]);
    after_operation();
}

void rpn_exp10()
{
    last_x = stack[0];
    __bid128_exp10(&stack[0], &stack[0]);
    after_operation();
}

void rpn_fact()
{
    // 整数は自前実装（精度改善）。非整数は x! = Γ(x + 1)
    last_x = stack[0];

    BID_UINT128 x = stack[0];
    // 整数判定: round_integral_exact の INEXACT が立たないか
    _IDEC_flags saved = _IDEC_glbflags;
    _IDEC_glbflags = BID_EXACT_STATUS;
    BID_UINT128 xi;
    bid128_round_integral_exact(&xi, &x);
    bool is_integer = ((_IDEC_glbflags & BID_INEXACT_EXCEPTION) == 0);
    _IDEC_glbflags = saved;

    if (is_integer)
    {
        // 非負整数のみ自前実装。負の整数はガンマにフォールバック。
        BID_UINT128 zero, one, two;
        bid128_from_string(&zero, "0");
        bid128_from_string(&one, "1");
        bid128_from_string(&two, "2");

        int is_neg = 0;
        bid128_quiet_less(&is_neg, &x, &zero);
        if (!is_neg)
        {
            // 0! = 1, 1! = 1
            int eq0 = 0, eq1 = 0;
            bid128_quiet_equal(&eq0, &xi, &zero);
            bid128_quiet_equal(&eq1, &xi, &one);
            if (eq0 || eq1)
            {
                stack[0] = one;
                after_operation();
                return;
            }

            // 2..n で累積乗算
            BID_UINT128 n = xi;
            BID_UINT128 i = two;
            BID_UINT128 acc = one;
            while (1)
            {
                int le = 0;
                bid128_quiet_less_equal(&le, &i, &n);
                if (!le)
                    break;
                __bid128_mul(&acc, &acc, &i);
                // 途中で∞になったら打ち切り
                int is_inf = 0;
                __bid128_isInf(&is_inf, &acc);
                if (is_inf)
                {
                    stack[0] = acc;
                    after_operation();
                    return;
                }
                // i++
                __bid128_add(&i, &i, &one);
            }
            stack[0] = acc;
            after_operation();
            return;
        }
        // 負の整数は未定義: Γ(x+1)へ
    }

    // 非整数 または 負の整数: x! = Γ(x + 1)
    {
        BID_UINT128 one, z, res;
        bid128_from_string(&one, "1");
        __bid128_add(&z, &stack[0], &one);
        __bid128_tgamma(&res, &z);
        stack[0] = res;
        after_operation();
    }
}

void rpn_logxy()
{
    // log_x(y)
    last_x = stack[0];
    BID_UINT128 ln_y, ln_x, res;
    __bid128_log(&ln_y, &stack[1]);
    __bid128_log(&ln_x, &stack[0]);
    __bid128_div(&res, &ln_y, &ln_x);
    stack_pop();
    stack[0] = res;
    after_operation();
}

// 逆三角関数（結果は角度モードに合わせて出力: RADはそのまま、DEGは*180/pi、GRADは*200/pi）
static void rpn_convert_angle_from_rad(BID_UINT128 *x)
{
    if (init_state.angle_mode == ANGLE_MODE_DEG)
    {
        BID_UINT128 k; // 180/pi
        bid128_from_string(&k, "57.295779513082320876798154814105170332");
        __bid128_mul(x, x, &k);
    }
    else if (init_state.angle_mode == ANGLE_MODE_GRAD)
    {
        BID_UINT128 k; // 200/pi
        bid128_from_string(&k, "63.661977236758134307553505349005744814");
        __bid128_mul(x, x, &k);
    }
}

void rpn_asin()
{
    last_x = stack[0];
    BID_UINT128 r;
    __bid128_asin(&r, &stack[0]); // radians
    rpn_convert_angle_from_rad(&r);
    stack[0] = r;
    after_operation();
}

void rpn_acos()
{
    last_x = stack[0];
    BID_UINT128 r;
    __bid128_acos(&r, &stack[0]); // radians
    rpn_convert_angle_from_rad(&r);
    stack[0] = r;
    after_operation();
}

void rpn_atan()
{
    last_x = stack[0];
    BID_UINT128 r;
    __bid128_atan(&r, &stack[0]); // radians
    rpn_convert_angle_from_rad(&r);
    stack[0] = r;
    after_operation();
}

// 双曲線関数（角度モードは適用しない: 引数は無次元としてそのまま扱う）
void rpn_sinh()
{
    last_x = stack[0];
    __bid128_sinh(&stack[0], &stack[0]);
    after_operation();
}

void rpn_cosh()
{
    last_x = stack[0];
    __bid128_cosh(&stack[0], &stack[0]);
    after_operation();
}

void rpn_tanh()
{
    last_x = stack[0];
    __bid128_tanh(&stack[0], &stack[0]);
    after_operation();
}

void rpn_asinh()
{
    last_x = stack[0];
    __bid128_asinh(&stack[0], &stack[0]);
    after_operation();
}

void rpn_acosh()
{
    last_x = stack[0];
    __bid128_acosh(&stack[0], &stack[0]);
    after_operation();
}

void rpn_atanh()
{
    last_x = stack[0];
    __bid128_atanh(&stack[0], &stack[0]);
    after_operation();
}

void rpn_last()
{
    // LAST X をXに復帰（pi/e/定数入力と同じスタック挙動）
    if (flag_state.push_flag)
    {
        stack_push();
    }
    clear_input_state();
    stack[0] = last_x;
    flag_state.push_flag = true;
    // 特殊値でもpushフラグ維持
}

// 設定アクセス（getterはそのまま、setterは後段で変更通知付き実装を提供）
disp_mode_t rpn_get_disp_mode() { return init_state.disp_mode; }
zero_mode_t rpn_get_zero_mode() { return init_state.zero_mode; }
angle_mode_t rpn_get_angle_mode() { return init_state.angle_mode; }
hyperbolic_mode_t rpn_get_hyperbolic_mode() { return init_state.hyperbolic_mode; }

// setterが呼ばれるたびに変更検出へ通知
static void notify_changed()
{
    settings_on_values_changed(init_state.disp_mode, init_state.angle_mode, init_state.hyperbolic_mode, init_state.zero_mode);
}
// setter（変更通知付き）
void rpn_set_disp_mode(disp_mode_t mode)
{
    init_state.disp_mode = mode;
    notify_changed();
}
void rpn_set_zero_mode(zero_mode_t mode)
{
    init_state.zero_mode = mode;
    notify_changed();
}
void rpn_set_angle_mode(angle_mode_t mode)
{
    init_state.angle_mode = mode;
    notify_changed();
}
void rpn_set_hyperbolic_mode(hyperbolic_mode_t mode)
{
    init_state.hyperbolic_mode = mode;
    notify_changed();
}

// #########################
//  定数入力
// #########################
void rpn_input_pi()
{
    // 入力中断して定数をXへ。必要なら push してから設定。
    if (flag_state.push_flag)
    {
        stack_push();
    }
    clear_input_state();
    bid128_from_string(&stack[0], "3.1415926535897932384626433832795028842");
    // 定数は確定値として扱うので、次の数値入力で push されるようにする
    flag_state.push_flag = true;
}

void rpn_input_e()
{
    if (flag_state.push_flag)
    {
        stack_push();
    }
    clear_input_state();
    bid128_from_string(&stack[0], "2.7182818284590452353602874713526624978");
    flag_state.push_flag = true;
}

// 設定保存（電源OFF直前にmainから呼ぶ）
void rpn_settings_maybe_save()
{
    save_settings();
}

// 入力中かどうかを返す
bool rpn_is_input_active()
{
    return input_state.input_len > 0;
}

int rpn_get_input_string(char *buf, int bufsize)
{
    if (!buf || bufsize <= 0)
        return 0;
    int n = input_state.input_len;
    if (n >= bufsize)
        n = bufsize - 1;
    for (int i = 0; i < n; ++i)
        buf[i] = input_state.input_str[i];
    buf[n] = '\0';
    return input_state.input_len;
}
