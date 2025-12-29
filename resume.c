#include "resume.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "RPN.h"
#include "settings.h"

// settings: 最終, macros: 末尾から2番目、本モジュール: 末尾から3番目
#define RESUME_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 3 * FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed))
{
    uint32_t magic;   // 'RSM1'
    uint32_t version; // 1
    uint32_t crc;     // data部のCRC32
    struct __attribute__((packed))
    {
        rpn_state_t rpn; // スタック/変数/LastX
    } data;
} resume_blob_t;

static const uint32_t RESUME_MAGIC = 0x52534D31u; // 'RSM1'
static const uint32_t RESUME_VERSION = 1u;

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

void resume_save_if_enabled(void)
{
    if (!settings_get_resume_enabled())
        return;

    resume_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = RESUME_MAGIC;
    blob.version = RESUME_VERSION;
    rpn_get_state(&blob.data.rpn);
    blob.crc = crc32_calc(&blob.data, sizeof(blob.data));

    // フラッシュ書込みはページ境界に揃える
    static uint8_t pad_buf[FLASH_SECTOR_SIZE];
    size_t raw_len = sizeof(blob);
    size_t page = FLASH_PAGE_SIZE;
    size_t write_len = (raw_len + (page - 1)) & ~(page - 1);
    if (write_len > FLASH_SECTOR_SIZE)
        write_len = FLASH_SECTOR_SIZE;
    memset(pad_buf, 0xFF, write_len);
    memcpy(pad_buf, &blob, raw_len);

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(RESUME_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(RESUME_FLASH_OFFSET, pad_buf, write_len);
    restore_interrupts(irq);
}

void resume_try_restore_on_boot(void)
{
    if (!settings_get_resume_enabled())
        return;
    const resume_blob_t *rom = (const resume_blob_t *)(XIP_BASE + RESUME_FLASH_OFFSET);
    if (rom->magic != RESUME_MAGIC || rom->version != RESUME_VERSION)
        return;
    uint32_t crc = crc32_calc(&rom->data, sizeof(rom->data));
    if (crc != rom->crc)
        return;
    // 復帰
    rpn_set_state(&rom->data.rpn);
}
