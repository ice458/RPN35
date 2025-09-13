// AQM1602A LCD Display (16x2) with I2C interface

#ifndef LCD_H
#define LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 初期化（I2C/ピン設定含む）
void lcd_init(void);
// 画面クリア
void lcd_clear(void);
// カーソル移動（row: 0-1, col: 0-15）
void lcd_set_cursor(uint8_t row, uint8_t col);
// 指定長の文字列を書き込み（折り返しなし、16文字まで推奨）
void lcd_write(const char *str, uint8_t length);
// null終端文字列を書き込み（16文字で打ち切り）
void lcd_write_str(const char *str);
// 行単位で上書き（不足分はスペースで埋める）
void lcd_write_line(uint8_t row, const char *str);
// カスタムキャラクタ登録（slot:0-7）
void lcd_define_custom_char(uint8_t slot, const uint8_t pattern[8]);
// よく使う矢印キャラクタの事前登録（slot 1,2を使用）
void lcd_init_arrow_chars(void);

// カスタム文字定義（矢印）
#define LCD_CHAR_UP_ARROW      1    // 上矢印（slot 1）
#define LCD_CHAR_DOWN_ARROW    2    // 下矢印（slot 2）
#define LCD_CHAR_UP_DOWN_ARROW 3    // 上下矢印（slot 3）
#define LCD_CHAR_MENU_ARROW    4    // メニュー右矢印（slot 4）

// 2行にまたがる長い文字列表示
// 32文字まで：1行目16文字、2行目16文字
void lcd_write_long_str(const char *str);
// 指定した開始位置から2行にまたがって表示
void lcd_write_long_str_at(uint8_t start_row, uint8_t start_col, const char *str);
// センタリングして2行表示（文字列が短い場合は1行目のみ使用）
void lcd_write_long_str_center(const char *str);

// 実用的な追加API
// 指定行を右詰めで表示（数値表示に便利）
void lcd_write_line_right(uint8_t row, const char *str);
// 2行の異なる文字列を一度に表示
void lcd_write_2lines(const char *line1, const char *line2);
// 数値をフォーマットして右詰め表示
void lcd_write_number_right(uint8_t row, const char *number);
// エラーメッセージを中央表示（一定時間後自動クリア）
void lcd_show_error(const char *message, uint16_t duration_ms);
// プログレスバー表示（0-100%）
void lcd_show_progress(uint8_t row, uint8_t percentage);
// 点滅表示（指定回数）
void lcd_blink_line(uint8_t row, const char *str, uint8_t blink_count);
// スクロール表示（長い文字列を左右にスクロール）
void lcd_scroll_text(uint8_t row, const char *text, uint16_t delay_ms);

// コントラスト設定（0-63）
void lcd_set_contrast(uint8_t contrast);

#ifdef __cplusplus
}
#endif

#endif