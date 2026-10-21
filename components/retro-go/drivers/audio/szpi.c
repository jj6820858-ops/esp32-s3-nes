// SPDX-License-Identifier: GPL-2.0-or-later
#include "rg_system.h"
#if RG_AUDIO_USE_SZPI_CODEC
#include <limits.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

// esp_codec_dev 1.3.6 API, matching the existing SZPI audio wiring.
// https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.3.6
static i2s_chan_handle_t tx;
static esp_codec_dev_handle_t speaker;
static const audio_codec_data_if_t *data_if;
static const audio_codec_ctrl_if_t *ctrl_if;
static const audio_codec_gpio_if_t *gpio_if;
static const audio_codec_if_t *codec_if;
static const char *last_error;
static int current_rate;

static bool driver_deinit(void)
{
    szpi_set_amplifier(false);
    if (speaker) { esp_codec_dev_close(speaker); esp_codec_dev_delete(speaker); speaker = NULL; }
    if (codec_if) { audio_codec_delete_codec_if(codec_if); codec_if = NULL; }
    if (ctrl_if) { audio_codec_delete_ctrl_if(ctrl_if); ctrl_if = NULL; }
    if (gpio_if) { audio_codec_delete_gpio_if(gpio_if); gpio_if = NULL; }
    if (data_if) { audio_codec_delete_data_if(data_if); data_if = NULL; }
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); tx = NULL; }
    return true;
}

static bool driver_set_rate(int rate)
{
    if (!speaker || rate <= 0) return false;
    if (rate == current_rate) return true;
    esp_codec_dev_sample_info_t fs = {.sample_rate = rate, .bits_per_sample = 16, .channel = 2};
    if (current_rate) esp_codec_dev_close(speaker);
    if (esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) return false;
    current_rate = rate;
    return true;
}

static bool driver_init(int device, int rate)
{
    (void)device;
    last_error = NULL;
    current_rate = 0;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = 6; channel.dma_frame_num = 180;
    if (i2s_new_channel(&channel, &tx, NULL) != ESP_OK) goto fail;
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk=38, .bclk=14, .ws=13, .dout=45, .din=I2S_GPIO_UNUSED},
    };
    if (i2s_channel_init_std_mode(tx, &cfg) != ESP_OK) goto fail;
    audio_codec_i2s_cfg_t data_cfg = {.port=0, .tx_handle=tx};
    audio_codec_i2c_cfg_t ctrl_cfg = {.port=0, .addr=ES8311_CODEC_DEFAULT_ADDR};
    data_if = audio_codec_new_i2s_data(&data_cfg);
    ctrl_if = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) goto fail;
    es8311_codec_cfg_t codec_cfg = {.ctrl_if=ctrl_if, .gpio_if=gpio_if,
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
    return speaker && count <= INT_MAX / sizeof(*frames) &&
        esp_codec_dev_write(speaker, (void *)frames, count * sizeof(*frames)) == ESP_CODEC_DEV_OK;
}

static bool driver_set_volume(int volume)
{
    return speaker && esp_codec_dev_set_out_vol(speaker, volume) == ESP_CODEC_DEV_OK;
}

static bool driver_set_mute(bool mute)
{
    if (!speaker) return false;
    if (mute) szpi_set_amplifier(false);
    if (esp_codec_dev_set_out_mute(speaker, mute) != ESP_CODEC_DEV_OK) return false;
    return mute || szpi_set_amplifier(true);
}

static const char *driver_error(void) { return last_error; }

const rg_audio_driver_t rg_audio_driver_szpi = {
    .name="szpi-es8311", .init=driver_init, .deinit=driver_deinit,
    .submit=driver_submit, .set_mute=driver_set_mute, .set_volume=driver_set_volume,
    .set_sample_rate=driver_set_rate, .get_error=driver_error,
};
#endif
