// AQM1602A LCD Display (16x2) with I2C interface

#include "LCD.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware_definition.h"

// タイムアウト付きI2C送信（LCD専用）
// 成功: 送信バイト数、失敗: 負のエラー値
static int lcd_i2c_write_with_retry(const uint8_t *buf, size_t len, bool nostop)
{
    if (!buf || len == 0)
        return 0;
    const int max_attempts = 8;        // 試行回数
    const uint32_t timeout_us = 10000; // 1回あたりの送信タイムアウト
    int last_err = -1;
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        // タイムアウト付き送信
        int ret = i2c_write_timeout_us(I2C_PORT, LCD_ADDR, buf, len, nostop, timeout_us);
        if (ret == (int)len)
            return ret; // 成功
        last_err = ret;

        // 3回目の失敗後にリカバリ（I2C再初期化）
        if (attempt == 3)
        {
            lcd_init();
        }
        // 少し待って再試行
        sleep_ms(5);
    }
    return last_err;
}

// 内部ユーティリティ
static void lcd_send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x80, cmd};
    lcd_i2c_write_with_retry(buf, 2, false);
    sleep_ms(1);
}

static void lcd_send_data_bytes(const uint8_t *data, uint8_t len)
{
    if (!data || len == 0)
        return;
    uint8_t buf[17];
    uint8_t n = (len > 16) ? 16 : len;
    buf[0] = 0x40;
    for (uint8_t i = 0; i < n; i++)
        buf[1 + i] = data[i];
    lcd_i2c_write_with_retry(buf, (size_t)(1 + n), false);
    sleep_ms(2);
}

void lcd_init(void)
{
    i2c_init(I2C_PORT, 100 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    gpio_init(nRST);
    gpio_set_dir(nRST, GPIO_OUT);

    uint8_t seq[9] = {0x38, 0x39, 0x14, 0x73, 0x56, 0x6c, 0x38, 0x0c, 0x01};
    gpio_put(nRST, 0);
    sleep_ms(5);
    gpio_put(nRST, 1);
    sleep_ms(5);
    for (uint8_t i = 0; i < 9; i++)
    {
        lcd_send_cmd(seq[i]);
    }
    lcd_init_arrow_chars();
}

// コントラスト設定
void lcd_set_contrast(uint8_t contrast)
{
    uint8_t c = contrast & 0x3F;
    lcd_send_cmd(0x39);
    lcd_send_cmd((uint8_t)(0x70 | (c & 0x0F)));
    uint8_t c54 = (uint8_t)((c >> 4) & 0x03);
    uint8_t cmd = (uint8_t)(0x54 | c54);
    lcd_send_cmd(cmd);
    lcd_send_cmd(0x38);
}

void lcd_clear(void)
{
    lcd_send_cmd(0x01);
}

void lcd_set_cursor(uint8_t row, uint8_t col)
{
    col = col % 16;
    row = row % 2;
    uint8_t cmd = 0x80 | col | (row << 6);
    lcd_send_cmd(cmd);
}

void lcd_write(const char *str, uint8_t length)
{
    if (!str || length == 0)
        return;
    uint8_t buf[16];
    uint8_t n = (length > 16) ? 16 : length;
    for (uint8_t i = 0; i < n; i++)
        buf[i] = (uint8_t)str[i];
    lcd_send_data_bytes(buf, n);
}

void lcd_write_str(const char *str)
{
    if (!str)
        return;
    uint8_t buf[16];
    uint8_t i = 0;
    while (i < 16 && str[i] != '\0')
    {
        buf[i] = (uint8_t)str[i];
        i++;
    }
    lcd_send_data_bytes(buf, i);
}

void lcd_write_line(uint8_t row, const char *str)
{
    lcd_set_cursor(row, 0);
    char line[16];
    uint8_t i = 0;
    for (; i < 16 && str && str[i] != '\0'; i++)
        line[i] = str[i];
    for (; i < 16; i++)
        line[i] = ' ';
    lcd_write(line, 16);
}

void lcd_define_custom_char(uint8_t slot, const uint8_t pattern[8])
{
    if (!pattern)
        return;
    lcd_send_cmd(0x40 | ((slot & 0x07) << 3));
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t d = pattern[i];
        uint8_t buf2[2] = {0x40, d};
        lcd_i2c_write_with_retry(buf2, 2, false);
        sleep_ms(1);
    }
    lcd_send_cmd(0x80);
}

void lcd_init_arrow_chars(void)
{
    static const uint8_t up[8] = {
        0b00000100,
        0b00001110,
        0b00010101,
        0b00000100,
        0b00000100,
        0b00000100,
        0b00000100,
        0b00000000,
    };
    static const uint8_t down[8] = {
        0b00000100,
        0b00000100,
        0b00000100,
        0b00000100,
        0b00010101,
        0b00001110,
        0b00000100,
        0b00000000,
    };
    static const uint8_t up_down[8] = {
        0b00000100,
        0b00001110,
        0b00010101,
        0b00000100,
        0b00010101,
        0b00001110,
        0b00000100,
        0b00000000,
    };
    static const uint8_t menu_arrow[8] = {
        0b00001000,
        0b00001100,
        0b00001110,
        0b00001111,
        0b00001110,
        0b00001100,
        0b00001000,
        0b00000000,
    };
    lcd_define_custom_char(1, up);
    lcd_define_custom_char(2, down);
    lcd_define_custom_char(3, up_down);
    lcd_define_custom_char(4, menu_arrow);
}

// 2行にまたがる長い文字列表示（最大32文字）
void lcd_write_long_str(const char *str)
{
    lcd_write_long_str_at(0, 0, str);
}

void lcd_write_long_str_at(uint8_t start_row, uint8_t start_col, const char *str)
{
    if (!str)
        return;

    // 文字列の長さを取得
    uint8_t len = 0;
    while (str[len] != '\0' && len < 32)
        len++;

    // 開始位置を正規化
    start_row = start_row % 2;
    start_col = start_col % 16;

    // 1行目の残り文字数を計算
    uint8_t first_line_remaining = 16 - start_col;

    // 1行目への書き込み
    lcd_set_cursor(start_row, start_col);
    if (len <= first_line_remaining)
    {
        // 1行目だけで収まる場合
        lcd_write_str(str);
    }
    else
    {
        // 1行目に収まらない場合
        char line1[17] = {0};
        for (uint8_t i = 0; i < first_line_remaining && i < len; i++)
        {
            line1[i] = str[i];
        }
        lcd_write(line1, first_line_remaining);

        // 2行目があれば書き込み
        if (len > first_line_remaining)
        {
            uint8_t second_row = (start_row + 1) % 2;
            lcd_set_cursor(second_row, 0);

            uint8_t remaining_chars = len - first_line_remaining;
            if (remaining_chars > 16)
                remaining_chars = 16;

            char line2[17] = {0};
            for (uint8_t i = 0; i < remaining_chars; i++)
            {
                line2[i] = str[first_line_remaining + i];
            }
            lcd_write(line2, remaining_chars);
        }
    }
}

void lcd_write_long_str_center(const char *str)
{
    if (!str)
        return;

    // 文字列の長さを取得
    uint8_t len = 0;
    while (str[len] != '\0' && len < 32)
        len++;

    if (len <= 16)
    {
        // 1行で収まる場合：1行目中央に配置
        uint8_t start_col = (16 - len) / 2;
        lcd_clear();
        lcd_set_cursor(0, start_col);
        lcd_write_str(str);
    }
    else
    {
        // 2行にまたがる場合
        uint8_t chars_per_line = len / 2;
        uint8_t extra = len % 2;

        // 1行目：(chars_per_line + extra)文字
        uint8_t line1_len = chars_per_line + extra;
        uint8_t line1_start = (16 - line1_len) / 2;

        // 2行目：chars_per_line文字
        uint8_t line2_len = chars_per_line;
        uint8_t line2_start = (16 - line2_len) / 2;

        lcd_clear();

        // 1行目
        lcd_set_cursor(0, line1_start);
        char line1[17] = {0};
        for (uint8_t i = 0; i < line1_len; i++)
        {
            line1[i] = str[i];
        }
        lcd_write(line1, line1_len);

        // 2行目
        lcd_set_cursor(1, line2_start);
        char line2[17] = {0};
        for (uint8_t i = 0; i < line2_len; i++)
        {
            line2[i] = str[line1_len + i];
        }
        lcd_write(line2, line2_len);
    }
}

// 指定行を右詰めで表示（数値表示に便利）
void lcd_write_line_right(uint8_t row, const char *str)
{
    if (!str)
        return;

    // 文字列長を取得
    uint8_t len = 0;
    while (str[len] != '\0' && len < 16)
        len++;

    // 右詰め位置を計算
    uint8_t start_col = (len < 16) ? (16 - len) : 0;

    // 行をクリアしてから右詰め表示
    char line[16];
    for (uint8_t i = 0; i < 16; i++)
        line[i] = ' ';
    for (uint8_t i = 0; i < len && i < 16; i++)
    {
        line[start_col + i] = str[i];
    }

    lcd_set_cursor(row, 0);
    lcd_write(line, 16);
}

// 2行の異なる文字列を一度に表示
void lcd_write_2lines(const char *line1, const char *line2)
{
    lcd_write_line(0, line1 ? line1 : "");
    lcd_write_line(1, line2 ? line2 : "");
}

// 数値をフォーマットして右詰め表示
void lcd_write_number_right(uint8_t row, const char *number)
{
    lcd_write_line_right(row, number);
}

// エラーメッセージを中央表示（一定時間後自動クリア）
void lcd_show_error(const char *message, uint16_t duration_ms)
{
    lcd_write_long_str_center(message);
    sleep_ms(duration_ms);
    lcd_clear();
}

// プログレスバー表示（0-100%）
void lcd_show_progress(uint8_t row, uint8_t percentage)
{
    char progress[17] = "[              ]";   // 16文字
    uint8_t filled = (percentage * 14) / 100; // 内側14文字分

    for (uint8_t i = 0; i < filled && i < 14; i++)
    {
        progress[1 + i] = '=';
    }

    lcd_set_cursor(row, 0);
    lcd_write(progress, 16);
}

// 点滅表示（指定回数）
void lcd_blink_line(uint8_t row, const char *str, uint8_t blink_count)
{
    for (uint8_t i = 0; i < blink_count; i++)
    {
        lcd_write_line(row, str);
        sleep_ms(500);
        lcd_write_line(row, "");
        sleep_ms(500);
    }
    lcd_write_line(row, str); // 最後は表示状態で終了
}

// スクロール表示（長い文字列を左右にスクロール）
void lcd_scroll_text(uint8_t row, const char *text, uint16_t delay_ms)
{
    if (!text)
        return;

    uint8_t text_len = 0;
    while (text[text_len] != '\0' && text_len < 64)
        text_len++; // 最大64文字

    if (text_len <= 16)
    {
        // スクロール不要
        lcd_write_line(row, text);
        return;
    }

    // 左スクロール
    for (uint8_t start = 0; start <= text_len - 16; start++)
    {
        char window[17] = {0};
        for (uint8_t i = 0; i < 16; i++)
        {
            window[i] = text[start + i];
        }
        lcd_set_cursor(row, 0);
        lcd_write(window, 16);
        sleep_ms(delay_ms);
    }
}
