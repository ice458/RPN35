#include "settings.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// 保存領域: 最終ページを1ページ確保（4096B）
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) // 末尾から1セクタ

typedef struct __attribute__((packed))
{
    uint32_t magic;          // 固定
    uint32_t version;        // 構造バージョン
    uint32_t crc;            // データ部CRC32（init_state_t のみ）
    init_state_t data;       // 設定（RPNコア連携）
    uint32_t auto_off_mode;  // 自動電源OFF設定（settings.h の auto_off_mode_t）
    uint32_t lcd_contrast;   // LCDコントラスト(0-63)
    // v4 追加項目
    uint32_t digits_value;   // 表示桁数: 0..9, 0xFF=ALL
    uint32_t last_key_mode;  // 0=Last X, 1=Undo
    uint32_t resume_enabled; // 0=OFF, 1=ON
} settings_blob_t;

static const uint32_t SETTINGS_MAGIC = 0x53544631; // 'STF1'
static const uint32_t SETTINGS_VERSION = 4;        // v4 で digits/last_key/resume を追加

static settings_blob_t g_loaded;
static bool g_have_loaded = false;
static bool g_dirty_since_boot = false;

// 簡易CRC32（Poly 0xEDB88320）
static uint32_t crc32_calc(const void *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    while (len--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1u)));
    }
    return ~crc;
}

static void defaults(init_state_t *s)
{
    s->angle_mode = ANGLE_MODE_DEG;
    s->disp_mode = DISP_MODE_NORMAL;
    s->hyperbolic_mode = HYPERBOLIC_MODE_OFF;
    s->zero_mode = ZERO_MODE_TRIM;
}

void settings_init(void)
{
    // フラッシュから読み出し
    const settings_blob_t *rom = (const settings_blob_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (rom->magic == SETTINGS_MAGIC && (rom->version == 1 || rom->version == 2 || rom->version == 3 || rom->version == SETTINGS_VERSION))
    {
        // v1とv2でCRCの取り方を切り分け
        if (rom->version == 1)
        {
            uint32_t crc = crc32_calc(&rom->data, sizeof(rom->data));
            if (crc == rom->crc)
            {
                memset(&g_loaded, 0, sizeof(g_loaded));
                g_loaded.magic = SETTINGS_MAGIC;
                g_loaded.version = SETTINGS_VERSION;
                g_loaded.data = rom->data;
                // v1 には auto_off_mode が無いのでデフォルトを採用（10分）
                g_loaded.auto_off_mode = (uint32_t)AUTO_OFF_10_MIN;
                // v1 には lcd_contrast が無いのでデフォルトを採用（中間値）
                g_loaded.lcd_contrast = 40u;
                // v4 追加分のデフォルト
                g_loaded.digits_value = 0xFFu;         // ALL
                g_loaded.last_key_mode = 0u;           // Last X
                g_loaded.resume_enabled = 0u;          // OFF
                // CRCをv2形式で再計算
                uint32_t new_crc = crc32_calc(&g_loaded.data, sizeof(g_loaded.data));
                g_loaded.crc = new_crc;
                g_have_loaded = true;
            }
        }
        else if (rom->version == 2)
        {
            // v2: data と auto_off_mode を含めて整合確認（CRCは data のみ維持仕様）
            uint32_t crc = crc32_calc(&rom->data, sizeof(rom->data));
            if (crc == rom->crc)
            {
                memset(&g_loaded, 0, sizeof(g_loaded));
                g_loaded.magic = SETTINGS_MAGIC;
                g_loaded.version = SETTINGS_VERSION;
                g_loaded.data = rom->data;
                g_loaded.auto_off_mode = rom->auto_off_mode;
                g_loaded.lcd_contrast = 40u; // 既存ユーザは中間値に初期化
                // v4 追加分のデフォルト
                g_loaded.digits_value = 0xFFu;         // ALL
                g_loaded.last_key_mode = 0u;           // Last X
                g_loaded.resume_enabled = 0u;          // OFF
                g_loaded.crc = rom->crc;
                g_have_loaded = true;
            }
        }
        else if (rom->version == 3)
        {
            // v3 現行（追加項目なし）
            uint32_t crc = crc32_calc(&rom->data, sizeof(rom->data));
            if (crc == rom->crc)
            {
                memset(&g_loaded, 0, sizeof(g_loaded));
                g_loaded.magic = SETTINGS_MAGIC;
                g_loaded.version = SETTINGS_VERSION;
                g_loaded.data = rom->data;
                g_loaded.auto_off_mode = rom->auto_off_mode;
                g_loaded.lcd_contrast = rom->lcd_contrast;
                // v4 追加分のデフォルト
                g_loaded.digits_value = 0xFFu;
                g_loaded.last_key_mode = 0u;
                g_loaded.resume_enabled = 0u;
                g_loaded.crc = rom->crc;
                g_have_loaded = true;
            }
        }
        else
        {
            // v3 現行
            uint32_t crc = crc32_calc(&rom->data, sizeof(rom->data));
            if (crc == rom->crc)
            {
                // v4 現行をそのままロード
                g_loaded = *rom;
                g_have_loaded = true;
            }
        }
    }
    if (!g_have_loaded)
    {
        memset(&g_loaded, 0, sizeof(g_loaded));
        g_loaded.magic = SETTINGS_MAGIC;
        g_loaded.version = SETTINGS_VERSION;
        defaults(&g_loaded.data);
        g_loaded.auto_off_mode = (uint32_t)AUTO_OFF_10_MIN; // デフォルトは10分
        g_loaded.lcd_contrast = 40u;                        // デフォルトは中間やや上
        g_loaded.digits_value = 0xFFu;                      // ALL
        g_loaded.last_key_mode = 0u;                        // Last X
        g_loaded.resume_enabled = 0u;                       // OFF
        g_loaded.crc = crc32_calc(&g_loaded.data, sizeof(g_loaded.data));
    }
    g_dirty_since_boot = false;
}

void settings_load_into(init_state_t *out)
{
    if (!out)
        return;
    if (!g_have_loaded)
        settings_init();
    *out = g_loaded.data;
}

void settings_on_values_changed(disp_mode_t disp, angle_mode_t angle, hyperbolic_mode_t hyperb, zero_mode_t zero)
{
    init_state_t now = {disp, angle, hyperb, zero};
    if (memcmp(&now, &g_loaded.data, sizeof(now)) != 0)
    {
        g_loaded.data = now;
        g_loaded.crc = crc32_calc(&g_loaded.data, sizeof(g_loaded.data));
        g_dirty_since_boot = true;
    }
}

void settings_save_if_dirty(void)
{
    if (!g_dirty_since_boot)
        return;
    // セクタ消去→書込み
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)&g_loaded, sizeof(g_loaded));
    restore_interrupts(ints);
    g_dirty_since_boot = false;
}

void settings_reset_to_defaults(void)
{
    // 既存のロード状態に関わらず、デフォルト値をセットして通常保存経路で書き込み
    if (!g_have_loaded)
        settings_init();
    // ヘッダは保持しつつ data と CRC を更新
    g_loaded.magic = SETTINGS_MAGIC;
    g_loaded.version = SETTINGS_VERSION;
    defaults(&g_loaded.data);
    g_loaded.auto_off_mode = (uint32_t)AUTO_OFF_10_MIN;
    g_loaded.lcd_contrast = 40u;
    g_loaded.digits_value = 0xFFu;
    g_loaded.last_key_mode = 0u;
    g_loaded.resume_enabled = 0u;
    g_loaded.crc = crc32_calc(&g_loaded.data, sizeof(g_loaded.data));
    g_have_loaded = true;
    g_dirty_since_boot = true;
    settings_save_if_dirty();
}

// ---- 自動電源OFF設定 API ----
auto_off_mode_t settings_get_auto_off_mode(void)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t m = g_loaded.auto_off_mode;
    if (m >= AUTO_OFF__COUNT)
        m = (uint32_t)AUTO_OFF_10_MIN;
    return (auto_off_mode_t)m;
}

void settings_set_auto_off_mode(auto_off_mode_t mode)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t nm = (uint32_t)mode;
    if (nm >= AUTO_OFF__COUNT)
        nm = (uint32_t)AUTO_OFF_10_MIN;
    if (g_loaded.auto_off_mode != nm)
    {
        g_loaded.auto_off_mode = nm;
        // CRCは data のみ対象のため変更不要。
        g_dirty_since_boot = true;
    }
}

uint32_t settings_get_auto_off_ms(void)
{
    switch (settings_get_auto_off_mode())
    {
    case AUTO_OFF_DISABLED:
        return 0u;
    case AUTO_OFF_3_MIN:
        return 3u * 60u * 1000u / 12u;
    case AUTO_OFF_5_MIN:
        return 5u * 60u * 1000u / 12u;
    case AUTO_OFF_10_MIN:
        return 10u * 60u * 1000u / 12u;
    default:
        return 10u * 60u * 1000u / 12u;
    }
}

// ---- LCD コントラスト ----
uint8_t settings_get_lcd_contrast(void)
{
    if (!g_have_loaded)
        settings_init();
    // 恒久的に 25..50 の範囲に制限
    uint32_t v = g_loaded.lcd_contrast & 0x3Fu;
    if (v < 25u)
        v = 25u;
    if (v > 50u)
        v = 50u;
    return (uint8_t)v;
}

void settings_set_lcd_contrast(uint8_t value)
{
    if (!g_have_loaded)
        settings_init();
    // 受け取った値を 25..50 にクランプ
    uint32_t v = (uint32_t)(value & 0x3F);
    if (v < 25u)
        v = 25u;
    if (v > 50u)
        v = 50u;
    if (g_loaded.lcd_contrast != v)
    {
        g_loaded.lcd_contrast = v;
        g_dirty_since_boot = true;
    }
}

// ---- Digits（表示桁数） ----
int8_t settings_get_digits(void)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t v = g_loaded.digits_value;
    if (v == 0xFFu)
        return -1; // ALL
    if (v > 9u)
        v = 9u;
    return (int8_t)v;
}

void settings_set_digits(int8_t digits)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t v;
    if (digits < 0)
        v = 0xFFu;
    else
    {
        if (digits > 9)
            digits = 9;
        v = (uint32_t)digits;
    }
    if (g_loaded.digits_value != v)
    {
        g_loaded.digits_value = v;
        // CRCは data のみ対象
        g_dirty_since_boot = true;
    }
}

// ---- Last Key モード ----
last_key_mode_t settings_get_last_key_mode(void)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t v = g_loaded.last_key_mode;
    return (v == 1u) ? LAST_KEY_UNDO : LAST_KEY_LAST_X;
}

void settings_set_last_key_mode(last_key_mode_t mode)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t v = (mode == LAST_KEY_UNDO) ? 1u : 0u;
    if (g_loaded.last_key_mode != v)
    {
        g_loaded.last_key_mode = v;
        g_dirty_since_boot = true;
    }
}

// ---- Resume ON/OFF ----
bool settings_get_resume_enabled(void)
{
    if (!g_have_loaded)
        settings_init();
    return g_loaded.resume_enabled ? true : false;
}

void settings_set_resume_enabled(bool enabled)
{
    if (!g_have_loaded)
        settings_init();
    uint32_t v = enabled ? 1u : 0u;
    if (g_loaded.resume_enabled != v)
    {
        g_loaded.resume_enabled = v;
        g_dirty_since_boot = true;
    }
}
