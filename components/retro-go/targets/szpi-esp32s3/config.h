// SPDX-License-Identifier: GPL-2.0-or-later
// 实战派板级适配入口：模拟器不关心引脚，只通过这些配置连接真实硬件。
// 接线来源：https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/
#include "szpi_board.h"
#define RG_TARGET_NAME "SZPI-ESP32-S3-N16R8"
#define RG_PROJECT_NAME "SZPI NES"
#define RG_CUSTOM_PLATFORM_INIT() szpi_platform_init()
#define RG_STORAGE_ROOT "/sd"
/* TF 卡使用 1 位 SDMMC；多分区选择在 szpi_storage.c 中完成。 */
#define RG_STORAGE_SDMMC_HOST SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED SDMMC_FREQ_DEFAULT
#define RG_GPIO_SDSPI_CLK 47
#define RG_GPIO_SDSPI_CMD 48
#define RG_GPIO_SDSPI_D0 21
#define RG_GPIO_I2C_SDA 1
#define RG_GPIO_I2C_SCL 2
#define RG_AUDIO_USE_INT_DAC 0
/* S3 使用板载 ES8311，不使用老 ESP32 的内部 DAC 驱动。 */
#define RG_AUDIO_USE_EXT_DAC 0
#define RG_AUDIO_USE_SZPI_CODEC 1
#define RG_SCREEN_DRIVER 2
/* 通过薄适配层连接 IDF 的 ST7789 驱动，初始化与原板级工程一致。 */
#define RG_SCREEN_HOST SPI3_HOST
#define RG_SCREEN_SPEED SPI_MASTER_FREQ_80M
#define RG_SCREEN_SPI_MODE 2
#define RG_SCREEN_WIDTH 320
#define RG_SCREEN_HEIGHT 240
#define RG_SCREEN_ROTATION 3
#define RG_SCREEN_RGB_BGR 0
#define RG_SCREEN_PARTIAL_UPDATES 0
#define RG_SCREEN_BACKLIGHT 1
#define RG_GPIO_LCD_MISO -1
#define RG_GPIO_LCD_MOSI 40
#define RG_GPIO_LCD_CLK 41
#define RG_GPIO_LCD_CS -1
/* CS 接到 PCA9557，并非 ESP32 的 GPIO，启动时通过 I2C 拉低。 */
#define RG_GPIO_LCD_DC 39
#define RG_GPIO_LCD_BCKL 42
#define RG_GPIO_LCD_BCKL_INVERT 1
#define RG_GAMEPAD_GPIO_MAP { {RG_KEY_MENU, .num=0, .pullup=1, .level=0}, }
/* 唯一实体 BOOT 键打开菜单；其余游戏键由双点触摸提供。 */
#define RG_RECOVERY_BTN RG_KEY_MENU
#define RG_BATTERY_DRIVER 0
#define RG_UPDATER_ENABLE 0
