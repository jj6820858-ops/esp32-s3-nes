#include "shared.h"

#include <nofrendo.h>

/* NES 与 Retro-Go 的衔接层：核心处理虚拟主机，框架处理菜单、文件和板级输出。
 * 学习主线：nes_main → input_update → nes_emulate → blit_screen / rg_audio_submit。
 * 画面使用 8 位色号索引；updates 的色板将色号转换为屏幕需要的 RGB565。
 */
static int overscan = true;
static int autocrop = 0;
static int palette = 0;
static bool slowFrame = false;
static bool nsfPlayer = false;
static nes_t *nes;

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

static const char *SETTING_AUTOCROP = "autocrop";
static const char *SETTING_OVERSCAN = "overscan";
static const char *SETTING_PALETTE = "palette";
static const char *SETTING_SPRITELIMIT = "spritelimit";
// 菜单通过下列回调访问模拟器，避免菜单直接操作 CPU、PPU 等内部状态。


static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        if (nsfPlayer)
            rg_display_clear(C_BLACK);
        else if (nes)
            (nes->blit_func)(NULL);
    }
}

static bool screenshot_handler(const char *filename, int width, int height)
{
	return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    return state_save(filename) == 0;
}

static bool load_state_handler(const char *filename)
{
    // 读档失败后硬复位，避免继续使用可能只恢复了一部分的主机状态。
    if (state_load(filename) != 0)
    {
        nes_reset(true);
        return false;
    }
    return true;
}

static bool reset_handler(bool hard)
{
    nes_reset(hard);
    return true;
}

static void build_palette(int n)
{
    // 两块缓冲共用相同的显示配色；交换字节后符合 RG_PIXEL_PAL565_BE 格式。
    uint16_t *pal = nofrendo_buildpalette(n, 16);
    for (int i = 0; i < 256; i++)
    {
        uint16_t color = (pal[i] >> 8) | ((pal[i]) << 8);
        updates[0]->palette[i] = color;
        updates[1]->palette[i] = color;
    }
    free(pal);
}

static rg_gui_event_t sprite_limit_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    bool spritelimit = ppu_getopt(PPU_LIMIT_SPRITES);

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        spritelimit = !spritelimit;
        rg_settings_set_number(NS_APP, SETTING_SPRITELIMIT, spritelimit);
        ppu_setopt(PPU_LIMIT_SPRITES, spritelimit);
    }

    strcpy(option->value, spritelimit ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static rg_gui_event_t overscan_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        overscan = !overscan;
        rg_settings_set_number(NS_APP, SETTING_OVERSCAN, overscan);
        return RG_DIALOG_REDRAW;
    }

    strcpy(option->value, overscan ? _("Auto") : _("Off"));

    return RG_DIALOG_VOID;
}

static rg_gui_event_t autocrop_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int val = autocrop;
    int max = 2;

    if (event == RG_DIALOG_PREV) val = val > 0 ? val - 1 : max;
    if (event == RG_DIALOG_NEXT) val = val < max ? val + 1 : 0;

    if (val != autocrop)
    {
        autocrop = val;
        rg_settings_set_number(NS_APP, SETTING_AUTOCROP, val);
        return RG_DIALOG_REDRAW;
    }

    if (val == 0) strcpy(option->value, _("Never"));
    if (val == 1) strcpy(option->value, _("Auto"));
    if (val == 2) strcpy(option->value, _("Always"));

    return RG_DIALOG_VOID;
}

static rg_gui_event_t palette_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int pal = palette;
    int max = NES_PALETTE_COUNT - 1;

    if (event == RG_DIALOG_PREV) pal = pal > 0 ? pal - 1 : max;
    if (event == RG_DIALOG_NEXT) pal = pal < max ? pal + 1 : 0;

    if (pal != palette)
    {
        palette = pal;
        rg_settings_set_number(NS_APP, SETTING_PALETTE, pal);
        build_palette(pal);
        return RG_DIALOG_REDRAW;
    }

    if (pal == NES_PALETTE_NOFRENDO)    strcpy(option->value, _("Nofrendo"));
    if (pal == NES_PALETTE_COMPOSITE)   strcpy(option->value, _("Composite"));
    if (pal == NES_PALETTE_NESCLASSIC)  strcpy(option->value, _("NES Classic"));
    if (pal == NES_PALETTE_NTSC)        strcpy(option->value, _("NTSC"));
    if (pal == NES_PALETTE_PVM)         strcpy(option->value, _("PVM"));
    if (pal == NES_PALETTE_SMOOTH)      strcpy(option->value, _("Smooth"));

    return RG_DIALOG_VOID;
}


static void blit_screen(uint8 *bmp)
{
    /* NES 核心在完成可见帧后回调此函数，框架显示任务再消费 currentUpdate。
     * 非阻塞查询上一帧的显示队列，记录压力供下一轮的跳帧判断使用。
     * bmp 为 NULL 时表示菜单请求重绘已有画面，不重新执行一帧游戏。
     */
    slowFrame = bmp && !rg_display_sync(false);
    // 按背景计数逐帧裁剪会使部分游戏抖动；上游建议用均值，下面的方案仍未启用。
    // int crop_h = (autocrop == 2) || (autocrop == 1 && nes->ppu->left_bg_counter > 210) ? 8 : 0;
    int crop_v = (overscan) ? nes->overscan : 0;
    int crop_h = (autocrop) ? 8 : 0;
    // crop_h = (autocrop == 2) || (autocrop == 1 && nes->ppu->left_bg_counter > 210) ? 8 : 0;
    // 仅改变可见窗口；行跨度仍保留核心两侧各 8 像素的额外绘制空间。
    currentUpdate->width = NES_SCREEN_WIDTH - crop_h * 2;
    currentUpdate->height = NES_SCREEN_HEIGHT - crop_v * 2;
    currentUpdate->offset = crop_v * currentUpdate->stride + crop_h + 8;
    rg_display_submit(currentUpdate, 0);
}

static void nsf_draw_overlay(void)
{
    extern int nsf_current_song;
    char song[32];
    const nsfheader_t *header = (nsfheader_t *)nes->cart->data_ptr;
    const rg_gui_option_t options[] = {
        {0, _("Name"),      (char*)header->name,      RG_DIALOG_FLAG_NORMAL, NULL},
        {0, _("Artist"),    (char*)header->artist,    RG_DIALOG_FLAG_NORMAL, NULL},
        {0, _("Copyright"), (char*)header->copyright, RG_DIALOG_FLAG_NORMAL, NULL},
        {0, _("Playing"),   (char*)song,              RG_DIALOG_FLAG_NORMAL, NULL},
        RG_DIALOG_END,
    };
    snprintf(song, sizeof(song), "%d / %d", nsf_current_song, header->total_songs);
    rg_gui_draw_dialog("NSF Player", options, 4, -1);
}


static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Palette"),      "-", RG_DIALOG_FLAG_NORMAL, &palette_update_cb};
    *dest++ = (rg_gui_option_t){0, _("Overscan"),     "-", RG_DIALOG_FLAG_NORMAL, &overscan_update_cb};
    *dest++ = (rg_gui_option_t){0, _("Crop sides"),   "-", RG_DIALOG_FLAG_NORMAL, &autocrop_update_cb};
    *dest++ = (rg_gui_option_t){0, _("Sprite limit"), "-", RG_DIALOG_FLAG_NORMAL, &sprite_limit_cb};
    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
}

void nes_main(void)
{
    /* 学习入口：保留上游模拟器，以下只负责 ROM、画面、声音和手柄的衔接。 */
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .event = &event_handler,
        .screenshot = &screenshot_handler,
        .options = &options_handler,
    };

    // 重新绑定 NES 的菜单回调，并取得启动器传入的 ROM 路径、存档槽和音频配置。
    app = rg_system_reinit(AUDIO_SAMPLE_RATE, &handlers, NULL);

    overscan = rg_settings_get_number(NS_APP, SETTING_OVERSCAN, 1);
    autocrop = rg_settings_get_number(NS_APP, SETTING_AUTOCROP, 0);
    palette = rg_settings_get_number(NS_APP, SETTING_PALETTE, NES_PALETTE_PVM);

    // 两块画面缓冲轮换：核心生成下一帧时，显示任务可继续处理刚提交的画面。
    updates[0] = rg_surface_create(NES_SCREEN_PITCH, NES_SCREEN_HEIGHT, RG_PIXEL_PAL565_BE, MEM_FAST);
    updates[1] = rg_surface_create(NES_SCREEN_PITCH, NES_SCREEN_HEIGHT, RG_PIXEL_PAL565_BE, MEM_FAST);
    currentUpdate = updates[0];

    /* 初始化红白机 CPU/PPU/APU；SYS_DETECT 根据游戏信息选择制式。 */
    nes = nes_init(SYS_DETECT, app->sampleRate, true, RG_BASE_PATH_BIOS "/fds_bios.bin");
    if (!nes)
        RG_PANIC("Init failed.");

    int ret = -1;

    if (rg_extension_match(app->romPath, "zip"))
    {
        // ZIP 先解压到内存，再走与普通文件相同的 ROM 解析、卡带插入流程。
        void *data;
        size_t size;
        if (!rg_storage_unzip_file(app->romPath, NULL, &data, &size, RG_FILE_ALIGN_8KB))
            RG_PANIC("ROM file unzipping failed!");
        ret = nes_insertcart(rom_loadmem(data, size));
    }
    else
    {
        /* 从 TF 卡读取 .nes 到运行内存，再按卡带 mapper 建立地址映射。 */
        ret = nes_loadfile(app->romPath);
    }

    if (ret == -1)
        RG_PANIC("ROM load failed.");
    else if (ret == -2)
        RG_PANIC("Unsupported mapper.");
    else if (ret == -3)
        RG_PANIC("BIOS file required.");
    else if (ret < 0)
        RG_PANIC("Unsupported ROM.");

    // 绑定输出回调；模拟器核心无需知道 SPI 引脚或 ST7789 初始化时序。
    nes->blit_func = blit_screen;

    nsfPlayer = nes->cart->type == ROM_TYPE_NSF;

    ppu_setopt(PPU_LIMIT_SPRITES, rg_settings_get_number(NS_APP, SETTING_SPRITELIMIT, 1));

    build_palette(palette);

    // 上游要求先推进两帧再恢复即时存档；原作者尚未说明该顺序的具体原因。
    // 这里保留原有行为，false 只关闭图像输出，CPU 和音频状态仍会推进。
    nes_emulate(false);
    nes_emulate(false);

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        rg_emu_load_state(app->saveSlot);
    }

    rg_system_set_tick_rate(nes->refresh_rate);

    int skipFrames = 0;

    while (true)
    {
#ifdef RG_TARGET_SZPI_ESP32S3
        szpi_console_poll();
#endif
        /* ① 获取实际按键状态：本板把双点触摸转换为 RG_KEY_*。 */
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU|RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        // 每轮仍推进一帧虚拟主机；skipFrames 决定是否为这一帧生成显示像素。
        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames && !nsfPlayer;
        int buttons = 0;

        // 框架按键位与 NES 串行手柄位定义不同，先显式转换后再传给核心。
        if (joystick & RG_KEY_START)  buttons |= NES_PAD_START;
        if (joystick & RG_KEY_SELECT) buttons |= NES_PAD_SELECT;
        if (joystick & RG_KEY_UP)     buttons |= NES_PAD_UP;
        if (joystick & RG_KEY_RIGHT)  buttons |= NES_PAD_RIGHT;
        if (joystick & RG_KEY_DOWN)   buttons |= NES_PAD_DOWN;
        if (joystick & RG_KEY_LEFT)   buttons |= NES_PAD_LEFT;
        if (joystick & RG_KEY_A)      buttons |= NES_PAD_A;
        if (joystick & RG_KEY_B)      buttons |= NES_PAD_B;

        if (drawFrame)
        {
            // 布尔比较得到 0 或 1，从而在两块缓冲间切换；跳帧时保留原指针。
            currentUpdate = updates[currentUpdate == updates[0]];
            nes_setvidbuf(currentUpdate->data);
        }

        /* ② 交给模拟的 1 号手柄；③ 推进一帧，CPU 执行游戏、PPU 生成画面。 */
        input_update(0, buttons);
        nes_emulate(drawFrame);
#ifdef RG_TARGET_SZPI_ESP32S3
        static unsigned diagnostic_frames;
        if (++diagnostic_frames%300==0)
            RG_LOGI("NES running: frames=%u pad=0x%04x",diagnostic_frames,(unsigned)joystick);
#endif

        // 先统计本轮模拟耗时，再提交音频；音频提交的等待不计入这段统计。
        rg_system_tick(rg_system_timer() - startTime);

        // ④ APU 每帧生成 PCM；本板经 I2S → ES8311 → 功放 → 喇叭输出。
        // samples_per_frame 是音频帧数，提交到驱动时的等待也参与运行节奏控制。
        rg_audio_submit((void*)nes->apu->buffer, nes->apu->samples_per_frame);

        if (skipFrames == 0)
        {
            // 以模拟制式的刷新率和倍速计算预算；固定跳帧优先，其次才按耗时调节。
            // 跳过的是图像生成与提交，游戏逻辑、手柄和每帧音频仍继续更新。
            int frameTime = 1000000 / (nes->refresh_rate * app->speed);
            int elapsed = rg_system_timer() - startTime;
            if (nsfPlayer)
                skipFrames = 10, nsf_draw_overlay();
            else if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > frameTime + 1500) // 允许少量耗时波动，避免频繁切换跳帧。
                skipFrames = 1; // (elapsed / frameTime)
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
    }

    RG_PANIC("Nofrendo died!");
}
