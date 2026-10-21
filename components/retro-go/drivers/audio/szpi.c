// SPDX-License-Identifier: GPL-2.0-or-later
#include "rg_system.h"
#if RG_AUDIO_USE_SZPI_CODEC
#include <limits.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

// 控制通路：I2C 配置 ES8311；数据通路：I2S 把 PCM 采样送到 ES8311。
// 下面通过 Retro-Go 原有音频驱动接口接入，无需改变 NES APU。
// https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.3.6
static i2s_chan_handle_t tx;
static esp_codec_dev_handle_t speaker;
static const audio_codec_data_if_t *data_if;
static const audio_codec_ctrl_if_t *ctrl_if;
static const audio_codec_gpio_if_t *gpio_if;
static const audio_codec_if_t *codec_if;
static const char *last_error;
static int current_rate;
/* tx_started 记录本层是否需要停止 I2S；codec_open 也覆盖 open 过程中失败的清理。
 * codec_dev 可能在开关格式时启停通道，两者必须同步更新，避免重复 disable。 */
static bool tx_started, codec_open;

static bool driver_deinit(void)
{
    /* 先关功放，再按“设备 → codec/控制/数据接口 → I2S 通道”的依赖顺序释放。
     * 每个资源都判空，既支持正常退出，也支持初始化在任意阶段失败后的回收。 */
    szpi_set_amplifier(false);
    if (speaker) {
        if (codec_open) { esp_codec_dev_close(speaker); tx_started = false; codec_open = false; }
        esp_codec_dev_delete(speaker); speaker = NULL;
    }
    if (codec_if) { audio_codec_delete_codec_if(codec_if); codec_if = NULL; }
    if (ctrl_if) { audio_codec_delete_ctrl_if(ctrl_if); ctrl_if = NULL; }
    if (gpio_if) { audio_codec_delete_gpio_if(gpio_if); gpio_if = NULL; }
    if (data_if) { audio_codec_delete_data_if(data_if); data_if = NULL; }
    if (tx) {
        if (tx_started) i2s_channel_disable(tx);
        i2s_del_channel(tx); tx = NULL; tx_started = false;
    }
    current_rate = 0;
    return true;
}

static bool driver_set_rate(int rate)
{
    /* 更换采样率需要重新打开 codec；只有成功后才更新 current_rate。
     * stereo + 16 bit 表示一个音频帧含左、右两个有符号 PCM 采样。 */
    /* 采样率描述每秒播放多少个采样，不是屏幕帧率。 */
    if (!speaker || rate <= 0) return false;
    if (rate == current_rate) return true;
    esp_codec_dev_sample_info_t fs = {.sample_rate = rate, .bits_per_sample = 16, .channel = 2};
    if (codec_open) { esp_codec_dev_close(speaker); codec_open = false; tx_started = false; }
    /* codec_dev 1.3.6 的 set_fmt 会先停止通道，调用前必须处于 RUNNING 状态。
     * 先开启 I2S 也让 MCLK 在 ES8311 配置期间持续存在。 */
    if (!tx_started) {
        if (i2s_channel_enable(tx) != ESP_OK) return false;
        tx_started = true;
    }
    codec_open = true; // 失败时也需要 close 清理由 codec_dev 开启的通道。
    if (esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) return false;
    current_rate = rate;
    return true;
}

static bool driver_init(int device, int rate)
{
    /* I2S0 的时钟、左右声道同步和数据引脚均与本板原接线一致。 */
    (void)device;
    last_error = NULL;
    current_rate = 0;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = 6; channel.dma_frame_num = 180;
    /* MCLK 为 codec 提供参考时钟；BCLK 按位移出数据；WS 区分左右声道；
     * DOUT 将 ESP32 的 PCM 送给 ES8311。这里仅播放，未申请 RX 通道。 */
    if (i2s_new_channel(&channel, &tx, NULL) != ESP_OK) goto fail;
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk=38, .bclk=14, .ws=13, .dout=45, .din=I2S_GPIO_UNUSED},
    };
    if (i2s_channel_init_std_mode(tx, &cfg) != ESP_OK) goto fail;
    if (i2s_channel_enable(tx) != ESP_OK) goto fail;
    tx_started = true;
    audio_codec_i2s_cfg_t data_cfg = {.port=0, .tx_handle=tx};
    audio_codec_i2c_cfg_t ctrl_cfg = {.port=0, .addr=ES8311_CODEC_DEFAULT_ADDR};
    data_if = audio_codec_new_i2s_data(&data_cfg);
    ctrl_if = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) goto fail;
    es8311_codec_cfg_t codec_cfg = {.ctrl_if=ctrl_if, .gpio_if=gpio_if,
        /* pa_pin=-1 表示不交给 codec 用直连 GPIO 控制，功放实际由 PCA9557 IO1 控制。 */
        .codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin=-1, .use_mclk=true,
        .hw_gain={.pa_voltage=5.0, .codec_dac_voltage=3.3}};
    codec_if = es8311_codec_new(&codec_cfg);
    if (!codec_if) goto fail;
    esp_codec_dev_cfg_t dev_cfg = {.codec_if=codec_if, .data_if=data_if, .dev_type=ESP_CODEC_DEV_TYPE_OUT};
    speaker = esp_codec_dev_new(&dev_cfg);
    if (!speaker || !driver_set_rate(rate)) goto fail;
    return true;
fail:
    last_error = "ES8311 / I2S initialization failed";
    driver_deinit();
    return false;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    /* count 是双声道帧数，codec 接口接收字节数；先限制乘法结果在 int 范围内。
     * 采样缓冲归调用方所有，本驱动只在写接口调用期间使用，不缓存其指针。 */
    /* APU 生成左右声道 16 位 PCM；codec 驱动负责提交给 I2S DMA。 */
    if (!speaker || count > INT_MAX / sizeof(*frames)) return false;
    int result = esp_codec_dev_write(speaker, (void *)frames, count * sizeof(*frames));
    if (result != ESP_CODEC_DEV_OK) {
        static bool reported;
        if (!reported) RG_LOGE("ES8311 PCM write failed: %d", result);
        reported = true;
    }
    return result == ESP_CODEC_DEV_OK;
}

static bool driver_set_volume(int volume)
{
    return speaker && esp_codec_dev_set_out_vol(speaker, volume) == ESP_CODEC_DEV_OK;
}

static bool driver_set_mute(bool mute)
{
    /* 关闭时先禁用功放，恢复时先解除 DAC 静音，再打开功放。 */
    if (!speaker) return false;
    if (mute) szpi_set_amplifier(false);
    if (esp_codec_dev_set_out_mute(speaker, mute) != ESP_CODEC_DEV_OK) return false;
    bool ready = mute || szpi_set_amplifier(true);
    RG_LOGI("ES8311 mute=%d PA port=0x%02x", mute, rg_i2c_read_byte(0x19, 0x01));
    return ready;
}

static const char *driver_error(void) { return last_error; }

const rg_audio_driver_t rg_audio_driver_szpi = {
    .name="szpi-es8311", .init=driver_init, .deinit=driver_deinit,
    .submit=driver_submit, .set_mute=driver_set_mute, .set_volume=driver_set_volume,
    .set_sample_rate=driver_set_rate, .get_error=driver_error,
};
#endif
