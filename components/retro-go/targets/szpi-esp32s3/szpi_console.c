// SPDX-License-Identifier: GPL-2.0-or-later
#include "rg_system.h"
#include "esp_rom_uart.h"

/* 复用上游输入任务读取 UART0，不新增任务，也不改模拟器的手柄接口。
 * 串口仅用于调试；实体触摸、BOOT 按键始终可以正常使用。 */
static volatile uint8_t action;

uint32_t szpi_console_read_keys(void)
{
    static uint32_t keys, deadline;
    uint32_t now=(uint32_t)(rg_system_timer()/1000);
    uint8_t c;
    for (int n=0;n<8 && esp_rom_uart_rx_one_char(&c)==0;n++) {
        uint32_t key=0;
        switch (c) {
        case 'r': key=RG_KEY_RIGHT; break; case 'l': key=RG_KEY_LEFT; break;
        case 'u': key=RG_KEY_UP; break; case 'd': key=RG_KEY_DOWN; break;
        case 'a': key=RG_KEY_A; break; case 'b': key=RG_KEY_B; break;
        case 't': key=RG_KEY_START; break; case 's': key=RG_KEY_SELECT; break;
        case 'm': key=RG_KEY_MENU; break; case 'o': key=RG_KEY_OPTION; break;
        case 'g': case 'h': case 'v': case 'c': case 'e': action=c; break;
        case '?': RG_LOGI("UART test: g=demo h=home v=LCD bars e=speaker tone c=TF crash log r/l/u/d=direction a/b t=start m=menu"); break;
        default: break;
        }
        if (key) { keys=key; deadline=now+250; RG_LOGI("UART pad: 0x%04x",(unsigned)key); }
    }
    return (int32_t)(deadline-now)>0 ? keys : 0;
}

void szpi_console_poll(void)
{
    /* 切换应用要回到主循环执行，不能在输入任务中卸载文件系统。 */
    uint8_t command=action; action=0;
    if (command=='e') {
        /* 绕过 NES APU，用 440Hz PCM 单独验证 I2S → DAC → 功放 → 喇叭。
         * 保留并恢复原音量；测试期间暂停游戏主循环，但继续喂系统统计。 */
        int volume=rg_audio_get_volume(), rate=rg_audio_get_sample_rate();
        bool mute=rg_audio_get_mute();
        rg_audio_set_mute(false); rg_audio_set_volume(75);
        RG_LOGI("Speaker tone begin: rate=%d PA=0x%02x DAC31=0x%02x DAC32=0x%02x",
            rate, rg_i2c_read_byte(0x19,0x01), rg_i2c_read_byte(0x18,0x31),
            rg_i2c_read_byte(0x18,0x32));
        rg_audio_frame_t pcm[256];
        for (unsigned offset=0; offset<(unsigned)rate*2; offset+=256) {
            size_t count=RG_MIN(256,(unsigned)rate*2-offset);
            for (size_t i=0;i<count;i++) {
                int16_t sample=(((offset+i)*880/(unsigned)rate)&1) ? 7000 : -7000;
                pcm[i]=(rg_audio_frame_t){sample,sample};
            }
            rg_audio_submit(pcm,count); rg_system_tick(0);
        }
        rg_audio_set_volume(volume); rg_audio_set_mute(mute);
        RG_LOGI("Speaker tone end");
    }
    if (command=='c') {
        /* 只读 TF 的崩溃记录，在主任务执行，避免与卸载文件系统并发。 */
        FILE *log=fopen(RG_STORAGE_ROOT "/crash.log", "r");
        if (log) {
            char line[256]; unsigned lines=0;
            RG_LOGI("TF crash.log begin");
            while (lines++<160 && fgets(line,sizeof(line),log)) fputs(line,stdout);
            fclose(log);
            RG_LOGI("TF crash.log end");
        } else RG_LOGI("TF crash.log unavailable");
    }
    if (command=='v') {
        /* 色条绕过游戏和菜单，单独验证显示链路。任何按键或 g/h 结束测试。 */
        rg_display_sync(true);
        rg_display_set_backlight(100);
        uint16_t row[RG_SCREEN_WIDTH];
        for (unsigned x=0;x<RG_SCREEN_WIDTH;x++)
            row[x]=x<RG_SCREEN_WIDTH/3 ? C_RED : x<RG_SCREEN_WIDTH*2/3 ? C_GREEN : C_BLUE;
        for (unsigned y=0;y<RG_SCREEN_HEIGHT;y++)
            rg_display_write_rect(0,y,RG_SCREEN_WIDTH,1,0,row,0);
        RG_LOGI("LCD color bars active; press any pad or send b/g/h to finish");
        while (!action && !rg_input_read_gamepad()) { rg_system_tick(0); rg_task_delay(20); }
        RG_LOGI("LCD color bars finished");
    }
    if (command=='h') rg_system_exit();
    if (command=='g') {
        const char *rom=RG_STORAGE_ROOT "/roms/nes/SZPI-Controls.nes";
        if (rg_storage_ready() && rg_storage_exists(rom))
            rg_system_switch_app("retro-core","nes",rom,RG_BOOT_NORMAL);
        else RG_LOGE("NES demo unavailable: TF not mounted or file missing");
    }
}
