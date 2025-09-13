// マクロ記録/再生とフラッシュ保存
#include "macro.h"
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define MACRO_SLOT_COUNT 3
#define MACRO_MAX_LEN 1024

typedef struct
{
    key_code_t seq[MACRO_MAX_LEN];
    int len;
} macro_slot_t;

static macro_slot_t g_slots[MACRO_SLOT_COUNT];
static bool g_recording = false;
static int g_rec_slot = -1;
static bool g_playing = false;
static int g_play_slot = -1;
static int g_play_index = 0;
static bool g_dirty_since_boot = false;

// フラッシュ保存レイアウト（設定とは別セクタを使用）
// settings.c は最終セクタ(末尾)を使用しているため、末尾から2番目のセクタを使用。
#define MACRO_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed))
{
    uint32_t magic;   // 固定 'MAC1'
    uint32_t version; // 構造バージョン
    uint32_t crc;     // data 部のCRC32
    struct
    {
        uint32_t len;
        uint8_t seq[MACRO_MAX_LEN]; // key_code_t は enum だが 1バイトに収まる定義
    } slots[MACRO_SLOT_COUNT];
} macro_blob_t;

static const uint32_t MACRO_MAGIC = 0x4D414331; // 'MAC1'
static const uint32_t MACRO_VERSION = 1;

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

static void macro_load_from_flash(void)
{
    const macro_blob_t *rom = (const macro_blob_t *)(XIP_BASE + MACRO_FLASH_OFFSET);
    bool ok = false;
    if (rom->magic == MACRO_MAGIC && rom->version == MACRO_VERSION)
    {
        uint32_t crc = crc32_calc(&rom->slots, sizeof(rom->slots));
        if (crc == rom->crc)
            ok = true;
    }
    if (ok)
    {
        memset(g_slots, 0, sizeof(g_slots));
        for (int i = 0; i < MACRO_SLOT_COUNT; i++)
        {
            int n = (int)rom->slots[i].len;
            if (n < 0)
                n = 0;
            if (n > MACRO_MAX_LEN)
                n = MACRO_MAX_LEN;
            g_slots[i].len = n;
            for (int j = 0; j < n; j++)
            {
                g_slots[i].seq[j] = (key_code_t)rom->slots[i].seq[j];
            }
        }
    }
    else
    {
        memset(g_slots, 0, sizeof(g_slots));
    }
    g_dirty_since_boot = false;
}

static void macro_save_to_flash(void)
{
    macro_blob_t blob;
    blob.magic = MACRO_MAGIC;
    blob.version = MACRO_VERSION;
    // copy
    for (int i = 0; i < MACRO_SLOT_COUNT; i++)
    {
        int n = g_slots[i].len;
        if (n < 0)
            n = 0;
        if (n > MACRO_MAX_LEN)
            n = MACRO_MAX_LEN;
        blob.slots[i].len = (uint32_t)n;
        // key_code_t を1バイトに
        memset(blob.slots[i].seq, 0, MACRO_MAX_LEN);
        for (int j = 0; j < n; j++)
            blob.slots[i].seq[j] = (uint8_t)g_slots[i].seq[j];
    }
    blob.crc = crc32_calc(&blob.slots, sizeof(blob.slots));

    // フラッシュ書込みは 256 バイト(FLASH_PAGE_SIZE)単位。長さを丸めてパディングする。
    const size_t raw_len = sizeof(blob);
    const size_t page = FLASH_PAGE_SIZE; // 256
    size_t write_len = (raw_len + (page - 1)) & ~(page - 1);
    if (write_len > FLASH_SECTOR_SIZE)
    {
        // 安全のため1セクタに制限
        write_len = FLASH_SECTOR_SIZE;
    }

    // パディング用バッファ（RAM上）。未使用領域は 0xFF で埋める。
    static uint8_t pad_buf[FLASH_SECTOR_SIZE];
    memset(pad_buf, 0xFF, write_len);
    memcpy(pad_buf, &blob, raw_len);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(MACRO_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(MACRO_FLASH_OFFSET, pad_buf, write_len);
    restore_interrupts(ints);
}

void macro_init(void)
{
    // フラッシュからロード
    macro_load_from_flash();
    g_recording = false;
    g_rec_slot = -1;
    g_playing = false;
    g_play_slot = -1;
    g_play_index = 0;
}

bool macro_is_recording(void) { return g_recording; }
bool macro_is_playing(void) { return g_playing; }

void macro_start_record(int slot)
{
    if (slot < 0 || slot >= MACRO_SLOT_COUNT)
        return;
    g_slots[slot].len = 0;
    g_recording = true;
    g_rec_slot = slot;
    // 再生が走っていたら止める
    g_playing = false;
    g_play_slot = -1;
    g_play_index = 0;
}

void macro_stop_record(void)
{
    g_recording = false;
    g_rec_slot = -1;
    // 記録セッションの結果（空=消去含む）を保存対象にする
    g_dirty_since_boot = true;
}

bool macro_has(int slot)
{
    if (slot < 0 || slot >= MACRO_SLOT_COUNT)
        return false;
    return g_slots[slot].len > 0;
}

bool macro_play(int slot)
{
    if (slot < 0 || slot >= MACRO_SLOT_COUNT)
        return false;
    if (g_slots[slot].len <= 0)
        return false;
    // 記録中なら停止
    g_recording = false;
    g_rec_slot = -1;
    // 再生準備
    g_playing = true;
    g_play_slot = slot;
    g_play_index = 0;
    return true;
}

void macro_cancel_play(void)
{
    g_playing = false;
    g_play_slot = -1;
    g_play_index = 0;
}

void macro_capture_event(key_event_t ev)
{
    if (!g_recording)
        return;
    // 基本は押下のみを記録、制御系は除外
    if (ev.type != KEY_EVENT_DOWN)
        return;
    // 記録対象外キー
    // マクロ専用キーは除外（PR開始/停止、P1/P2/P3再生）
    // K_MODE はメニュー操作のため除外（UI内での選択は別UIのハンドラが処理）
    if (ev.code == K_PR || ev.code == K_P1 || ev.code == K_P2 || ev.code == K_P3 || ev.code == K_MODE || ev.code == K_OFF)
        return;
    if (g_rec_slot < 0 || g_rec_slot >= MACRO_SLOT_COUNT)
        return;
    macro_slot_t *s = &g_slots[g_rec_slot];
    if (s->len >= MACRO_MAX_LEN)
    {
        // これ以上記録できないので自動停止
        macro_stop_record();
        return;
    }
    s->seq[s->len++] = ev.code;
    g_dirty_since_boot = true;
    // 直後に満了した場合も停止
    if (s->len >= MACRO_MAX_LEN)
    {
        macro_stop_record();
    }
}

bool macro_inject_next(key_event_t *out_ev)
{
    if (!out_ev)
        return false;
    if (!g_playing || g_play_slot < 0)
        return false;
    macro_slot_t *s = &g_slots[g_play_slot];
    if (g_play_index >= s->len)
    {
        // 終了
        g_playing = false;
        g_play_slot = -1;
        g_play_index = 0;
        return false;
    }
    // 1イベント注入（DOWNのみ）
    out_ev->type = KEY_EVENT_DOWN;
    out_ev->code = s->seq[g_play_index++];
    return true;
}

void macro_save_if_dirty(void)
{
    if (!g_dirty_since_boot)
        return;
    macro_save_to_flash();
    g_dirty_since_boot = false;
}

void macro_reset_all(void)
{
    // 記録・再生状態は解除
    g_recording = false;
    g_rec_slot = -1;
    g_playing = false;
    g_play_slot = -1;
    g_play_index = 0;

    // 全スロット消去
    for (int i = 0; i < MACRO_SLOT_COUNT; ++i)
    {
        g_slots[i].len = 0;
        memset(g_slots[i].seq, 0, sizeof(g_slots[i].seq));
    }
    // 即時保存
    macro_save_to_flash();
    g_dirty_since_boot = false;
}
