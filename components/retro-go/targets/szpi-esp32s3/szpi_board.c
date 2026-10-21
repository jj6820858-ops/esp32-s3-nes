// SPDX-License-Identifier: GPL-2.0-or-later
#include "rg_system.h"
#include "szpi_touch.h"
#include <driver/i2c.h>

/* PCA9557 的 IO0 控制屏幕片选，IO1 控制功放，IO2 控制摄像头掉电。 */
static uint8_t pca_output = 0x05; // 屏幕暂不选中、功放关闭、摄像头掉电。
static bool touch_ready;

void szpi_platform_init(void)
{
    /* 与旧工程相同的接线：I2C0 SDA=GPIO1、SCL=GPIO2。 */
    RG_ASSERT(rg_i2c_init(), "Board I2C initialization failed");
    RG_ASSERT(rg_i2c_write_byte(0x19, 0x01, pca_output), "PCA9557 output preset failed");
    RG_ASSERT(rg_i2c_write_byte(0x19, 0x03, 0xF8), "PCA9557 configuration failed");
    /* 此时保持 CS=1！GPIO40/41 还未接到 SPI，切换复用和时钟空闲电平时
     * 可能产生边沿。只有显示驱动完成 SPI 配置后才能选中屏幕，否则控制器
     * 可能把这些边沿当成数据，导致整条命令流错位，表现为背光亮但无图像。 */
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
    // 一次读取两个触点；通信失败返回 0，避免角色因粘键一直移动。
    if (i2c_master_write_read_device(I2C_NUM_0, 0x38, &reg, 1, packet, sizeof(packet),
                                      pdMS_TO_TICKS(20)) != ESP_OK) return 0;
    return szpi_touch_decode(packet, sizeof(packet));
}

bool szpi_set_amplifier(bool enabled)
{
    /* 只改变功放对应的 bit1，保留屏幕片选与摄像头的状态。 */
    uint8_t output = (pca_output & ~0x02) | (enabled ? 0x02 : 0);
    if (!rg_i2c_write_byte(0x19, 0x01, output)) return false;
    pca_output = output;
    return true;
}

bool szpi_set_lcd_selected(bool selected)
{
    /* CS 低有效；只改变 bit0，保留功放和摄像头状态。 */
    uint8_t output = (pca_output & ~0x01) | (selected ? 0 : 0x01);
    if (!rg_i2c_write_byte(0x19, 0x01, output)) return false;
    pca_output = output;
    return true;
}
