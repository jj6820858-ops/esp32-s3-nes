// SPDX-License-Identifier: GPL-2.0-or-later
// Retro-Go 显示接口 → Espressif esp_lcd → SPI3 → ST7789。
// 本文件由 rg_display.c 包含，沿用其 LCD_BUFFER_LENGTH 和 lcd_* 接口。
#include <driver/spi_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

static esp_lcd_panel_io_handle_t szpi_lcd_io;
static esp_lcd_panel_handle_t szpi_lcd_panel;
static uint16_t *szpi_lcd_dma;
/* 单块 DMA 暂存区供显示任务反复使用；模拟器画面可在其他内存中，
 * 发送前由公共显示层转换、拷贝到此处，不能在 DMA 尚未完成时覆盖。 */

static void lcd_sync(void)
{
    // -1 表示不发送新命令；该调用等待之前的颜色 DMA 事务全部完成。
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(szpi_lcd_io, -1, NULL, 0));
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    /* length 和 pixels 的单位是像素；分配量、传输量再乘 2 得到 RGB565 字节数。 */
    RG_ASSERT(length <= LCD_BUFFER_LENGTH, "LCD staging buffer too small");
    return szpi_lcd_dma;
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t pixels)
{
    if (!pixels) return;
    RG_ASSERT(buffer == szpi_lcd_dma && pixels <= LCD_BUFFER_LENGTH, "Invalid LCD buffer");
    // 像素已由 Retro-Go 转成 RGB565 大端。延续当前窗口，不重复发送 RAMWR。
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(szpi_lcd_io, -1, buffer, pixels * 2));
    // 先确认 DMA 完成再复用同一缓冲，避免显示任务改写仍在传输的数据。
    lcd_sync();
}

static void lcd_set_window(int left, int top, int width, int height)
{
    /* 0x2A/0x2B 指定列、行范围，结束坐标包含在窗口内，所以要减 1。
     * 随后的 0x2C 开始显存写入；分块像素传输继续使用这个窗口。 */
    RG_ASSERT(left >= 0 && top >= 0 && width > 0 && height > 0 &&
        left + width <= RG_SCREEN_WIDTH && top + height <= RG_SCREEN_HEIGHT,
        "LCD window out of bounds");
    int right = left + width - 1, bottom = top + height - 1;
    uint8_t xs[] = {left >> 8, left & 255, right >> 8, right & 255};
    uint8_t ys[] = {top >> 8, top & 255, bottom >> 8, bottom & 255};
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(szpi_lcd_io, 0x2a, xs, sizeof(xs)));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(szpi_lcd_io, 0x2b, ys, sizeof(ys)));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(szpi_lcd_io, 0x2c, NULL, 0));
}

static void lcd_set_backlight(float percent)
{
    unsigned duty = (unsigned)(1023 * RG_MIN(RG_MAX(percent / 100.f, 0.f), 1.f));
    // 与原板级例程一致：10 位、5kHz、GPIO42 反相输出；亮度立即生效。
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    RG_LOGI("ST7789 backlight: %.0f%%, duty=%u/1023", percent, duty);
}

static void lcd_init(void)
{
    /* 初始化跨两条总线：I2C 保持 LCD 不选中 → 配置 SPI/引脚复用 →
     * 建立空闲时钟 → I2C 选中 LCD → 有效复位和控制器初始化 → 分配 DMA 缓存。 */
    RG_ASSERT(rg_i2c_read_byte(0x19, 0x01) & 1, "LCD CS must remain high during SPI setup");
    const ledc_timer_config_t timer = {
        .speed_mode=LEDC_LOW_SPEED_MODE, .duty_resolution=LEDC_TIMER_10_BIT,
        .timer_num=LEDC_TIMER_0, .freq_hz=5000, .clk_cfg=LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t backlight = {
        .gpio_num=RG_GPIO_LCD_BCKL, .speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0, .timer_sel=LEDC_TIMER_0, .duty=0,
        .flags.output_invert=true,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&backlight));
    const spi_bus_config_t bus = {
        .sclk_io_num=RG_GPIO_LCD_CLK, .mosi_io_num=RG_GPIO_LCD_MOSI,
        .miso_io_num=-1, .quadwp_io_num=-1, .quadhd_io_num=-1,
        .max_transfer_sz=LCD_BUFFER_LENGTH * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(RG_SCREEN_HOST, &bus, SPI_DMA_CH_AUTO));
    const esp_lcd_panel_io_spi_config_t io = {
        /* cs_gpio_num=-1：SPI 驱动不切换 CS，实际片选由 PCA9557 维持低电平。 */
        .dc_gpio_num=RG_GPIO_LCD_DC, .cs_gpio_num=-1, .pclk_hz=RG_SCREEN_SPEED,
        .lcd_cmd_bits=8, .lcd_param_bits=8, .spi_mode=RG_SCREEN_SPI_MODE,
        .trans_queue_depth=2,
    };
    // esp_lcd 在这里同时配置 D/C 方向和 IO MUX，而不只是设置输出使能。
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)RG_SCREEN_HOST,
                                          &io, &szpi_lcd_io));
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num=-1, .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel=16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(szpi_lcd_io, &panel, &szpi_lcd_panel));
    /* CS=1 时先完成一次 SPI 事务，使 mode 2 的空闲高电平实际建立。
     * 此次命令不会被屏幕接收；选中屏幕后还要再发送一次有效复位。 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(szpi_lcd_panel));
    /* 原板级工程也是在 SPI 就绪后才拉低扩展芯片上的 CS。
     * 不能在 platform_init 阶段提前选中屏幕，否则总线初始化的跳变可能被采样。 */
    RG_ASSERT(szpi_set_lcd_selected(true), "LCD chip select failed");
    rg_usleep(1000);
    int output=rg_i2c_read_byte(0x19, 0x01), direction=rg_i2c_read_byte(0x19, 0x03);
    RG_LOGI("LCD PCA9557: output=0x%02x config=0x%02x (CS bit0 must be 0)", output, direction);
    RG_ASSERT(output >= 0 && !(output & 1) && direction >= 0 && !(direction & 1),
              "LCD chip select is not active");
    // IDF 驱动负责软件复位、SLPOUT 后的等待以及 MADCTL/COLMOD/RAMCTRL。
    ESP_ERROR_CHECK(esp_lcd_panel_reset(szpi_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(szpi_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(szpi_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(szpi_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(szpi_lcd_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(szpi_lcd_panel, true));
    szpi_lcd_dma=rg_alloc(LCD_BUFFER_LENGTH * 2, MEM_DMA);
    RG_LOGI("ST7789 IDF driver ready: SPI3 mode=%d speed=%d RGB565 MADCTL=0x60",
            RG_SCREEN_SPI_MODE, RG_SCREEN_SPEED);
}

static void lcd_deinit(void)
{
    /* 先等 DMA 停止，再关显示并释放句柄/缓存，防止传输访问已释放内存。
     * 释放 CS 仍要写 PCA9557，因此系统关闭时必须先反初始化显示，再关闭 I2C。 */
    lcd_sync();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(szpi_lcd_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_del(szpi_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_io_del(szpi_lcd_io));
    RG_ASSERT(szpi_set_lcd_selected(false), "LCD chip deselect failed");
    ESP_ERROR_CHECK(spi_bus_free(RG_SCREEN_HOST));
    free(szpi_lcd_dma); szpi_lcd_dma=NULL;
    szpi_lcd_panel=NULL; szpi_lcd_io=NULL;
}
