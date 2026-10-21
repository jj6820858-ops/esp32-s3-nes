// SPDX-License-Identifier: GPL-2.0-or-later
#include "rg_system.h"
#include "szpi_touch.h"
#include <driver/i2c.h>

static uint8_t pca_output = 0x05; // LCD CS high, PA off, camera powered down.
static bool touch_ready;

void szpi_platform_init(void)
{
    RG_ASSERT(rg_i2c_init(), "Board I2C initialization failed");
    RG_ASSERT(rg_i2c_write_byte(0x19, 0x01, pca_output), "PCA9557 output preset failed");
    RG_ASSERT(rg_i2c_write_byte(0x19, 0x03, 0xF8), "PCA9557 configuration failed");
    pca_output = 0x04; // LCD CS low, PA off; camera stays powered down.
    RG_ASSERT(rg_i2c_write_byte(0x19, 0x01, pca_output), "LCD chip select failed");
    uint8_t reg = 0xA8, id = 0;
    touch_ready = i2c_master_write_read_device(I2C_NUM_0, 0x38, &reg, 1, &id, 1,
                                              pdMS_TO_TICKS(50)) == ESP_OK;
    RG_LOGI("SZPI: PSRAM=%u KiB; FT6336/FT5x06 %s (ID 0x%02X)",
            (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024),
            touch_ready ? "found" : "not detected", id);
}

uint32_t szpi_read_touch(void)
{
    if (!touch_ready) return 0;
    uint8_t reg = 0x02, packet[13];
    // IDF's I2C driver serializes transactions; a failed read releases all keys.
    if (i2c_master_write_read_device(I2C_NUM_0, 0x38, &reg, 1, packet, sizeof(packet),
                                      pdMS_TO_TICKS(20)) != ESP_OK) return 0;
    return szpi_touch_decode(packet, sizeof(packet));
}

bool szpi_set_amplifier(bool enabled)
{
    uint8_t output = (pca_output & ~0x02) | (enabled ? 0x02 : 0);
    if (!rg_i2c_write_byte(0x19, 0x01, output)) return false;
    pca_output = output;
    return true;
}
