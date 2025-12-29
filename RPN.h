#ifndef RPN_H
#define RPN_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#define BID_THREAD
#define DECIMAL_CALL_BY_REFERENCE 1
#define DECIMAL_GLOBAL_ROUNDING 1
#define DECIMAL_GLOBAL_EXCEPTION_FLAGS 1
#include "bid_conf.h"
#include "bid_functions.h"

#define MAX_INPUTVAL_LENGTH 40 // 入力中の数値の最大桁数（小数点、符号含む）

    typedef struct
    {
        char input_str[MAX_INPUTVAL_LENGTH]; // 入力中の文字列
        int8_t input_len;                    // 入力中の文字列の長さ
        bool input_sign;                     // 符号。true:マイナス、false:プラス
        int8_t dot_pos;                      // 小数点の位置。小数点が無いときは-1
        int8_t exp_pos;                      // 指数の位置。指数が無いときは-1
        bool exp_sign;                       // 指数の符号。true:マイナス、false:プラス
    } input_state_t;

    typedef struct
    {
        bool push_flag; // 入力開始時にpushするかどうかのフラグ
    } flag_state_t;

    // #########################
    //  設定項目
    // #########################
    // 表示モード
    typedef enum
    {
        DISP_MODE_NORMAL,
        DISP_MODE_SCIENTIFIC,
        DISP_MODE_ENGINEERING
    } disp_mode_t;

    // 小数部末尾ゼロの表示モード
    typedef enum
    {
        ZERO_MODE_PAD, // 埋める（末尾ゼロを表示）
        ZERO_MODE_TRIM // 省く（末尾ゼロを非表示）
    } zero_mode_t;

    // 角度モード
    typedef enum
    {
        ANGLE_MODE_DEG,
        ANGLE_MODE_RAD,
        ANGLE_MODE_GRAD
    } angle_mode_t;
    // 双曲線関数モード
    typedef enum
    {
        HYPERBOLIC_MODE_OFF,
        HYPERBOLIC_MODE_ON
    } hyperbolic_mode_t;

    // Flashに保存する設定
    typedef struct
    {
        disp_mode_t disp_mode;
        angle_mode_t angle_mode;
        hyperbolic_mode_t hyperbolic_mode;
        zero_mode_t zero_mode; // 小数部末尾ゼロの表示モード
    } init_state_t;

    // ---- 公開関数 ----
    void bid128_to_str(BID_UINT128 x, char *buf, int bufsize);
    void init_rpn();

    // RPNコアAPI（ハード非依存）
    // スタックの現在値を取得
    BID_UINT128 rpn_stack_x();
    BID_UINT128 rpn_stack_y();
    BID_UINT128 rpn_stack_z();
    BID_UINT128 rpn_stack_t();
    // Xレジスタを直接設定（入力状態はクリア）
    void rpn_set_x(BID_UINT128 x);

    // 変数操作（VA..VF）
    typedef enum
    {
        RPN_VAR_OP_NONE = 0,
        RPN_VAR_OP_ST,
        RPN_VAR_OP_LD,
        RPN_VAR_OP_CLR,
    } rpn_var_op_t;
    // オペレータの選択（ST/LD/CLR）を設定
    void rpn_var_set_pending_op(rpn_var_op_t op);
    // 現在選択中のオペレータを取得
    rpn_var_op_t rpn_var_get_pending_op(void);
    // VA..VF に相当するスロット番号(0..5)を適用。実行されたらtrue
    bool rpn_var_apply_slot(int slot_idx);
    // 表示用のインジケータ文字（'S','L','C' または '\0'）
    char rpn_var_indicator_char(void);

    // 科学定数（C1/C2）
    // グループは 1 or 2、インデックスは 0..9（0..8=1..9, 9=0キー）
    bool rpn_const_apply(int group, int index);
    const char *rpn_const_symbol(int group, int index); // UI表示用（任意）
    const char *rpn_const_name(int group, int index);   // 下段説明用（任意）
    int rpn_const_group_size(int group);                // 常に10
    // 入力編集系
    void rpn_input_clear();
    void rpn_input_append_digit(char digit); // '0'..'9'
    void rpn_input_dot();                    // '.'
    void rpn_input_exp();                    // 'E'
    void rpn_input_toggle_sign();
    void rpn_input_backspace(); // ← バックスペース
    void rpn_clear_x();         // Xを0にクリア
    // 入力をXに反映して確定するが、スタックはpushしない
    void rpn_commit_input_without_push();
    // 入力中かどうか（入力バッファに1文字以上あるか）
    bool rpn_is_input_active();
    // 入力中の生文字列を取得（NULL終端）。返り値は文字列長。bufsize>=1必須。
    int rpn_get_input_string(char *buf, int bufsize);
    // スタック操作
    void rpn_enter(); // push X
    void rpn_swap();
    void rpn_roll_up();
    void rpn_roll_down();
    // 二項演算
    void rpn_add();
    void rpn_sub();
    void rpn_mul();
    void rpn_div();
    void rpn_logxy();    // xを底とするyの対数
    void rpn_nth_root(); // y√x （y: Y, x: X）
    // 単項演算
    void rpn_rev();  // 1/x
    void rpn_fact(); // x!
    void rpn_sqrt();
    void rpn_pow2();
    void rpn_pow();  // Y^X
    void rpn_cube(); // X^3
    void rpn_cbrt(); // 3√X
    void rpn_log();
    void rpn_ln();
    void rpn_exp();   // e^X
    void rpn_exp10(); // 10^X
    void rpn_sin();
    void rpn_cos();
    void rpn_tan();
    void rpn_asin();
    void rpn_acos();
    void rpn_atan();
    void rpn_sinh();
    void rpn_cosh();
    void rpn_tanh();
    void rpn_asinh();
    void rpn_acosh();
    void rpn_atanh();
    void rpn_last(); // LAST X をXに復帰
    // Undo（スタック全体復帰。Lastキー設定がUndoのとき使用）
    void rpn_undo();
    // 定数入力
    void rpn_input_pi(); // π
    void rpn_input_e();  // e

    // 設定アクセス（表示モード）
    void rpn_set_disp_mode(disp_mode_t mode);
    disp_mode_t rpn_get_disp_mode();

    // 設定アクセス（末尾ゼロ表示モード）
    void rpn_set_zero_mode(zero_mode_t mode);
    zero_mode_t rpn_get_zero_mode();

    // 設定アクセス（角度/双曲線モード）
    void rpn_set_angle_mode(angle_mode_t mode);
    angle_mode_t rpn_get_angle_mode();
    void rpn_set_hyperbolic_mode(hyperbolic_mode_t mode);
    hyperbolic_mode_t rpn_get_hyperbolic_mode();

    // 直近演算で発生した例外フラグ（BID_*_EXCEPTION の OR）を取得
    _IDEC_flags rpn_get_last_exceptions();

    // 設定の永続化: 電源OFF前などに変更があればFlashに保存
    void rpn_settings_maybe_save();

    // Undoバッファ操作
    void rpn_undo_clear(void);
    // マクロ開始直前などユーザ境界で明示的にキャプチャ
    void rpn_undo_capture_boundary(void);

    // リセット系（Resetサブメニュー用）
    void rpn_reset_stack_only(void); // X,Y,Z,T と Last X、入力状態、Undo クリア
    void rpn_reset_vars_only(void);  // 変数A..F のみクリア
    void rpn_reset_memory(void);     // Stack + Vars をクリア

    // レジューム用にRPN状態を取得/設定
    typedef struct
    {
        BID_UINT128 x, y, z, t;
        BID_UINT128 last_x;
        BID_UINT128 vars[6];
    } rpn_state_t;
    void rpn_get_state(rpn_state_t *out);
    void rpn_set_state(const rpn_state_t *st);

#ifdef __cplusplus
}
#endif

#endif