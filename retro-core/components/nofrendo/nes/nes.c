/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** nes.c: NES console emulation
**
*/

#include "nes.h"

/* 虚拟主机总调度：把 CPU 指令、PPU 扫描线、APU 计时和卡带 mapper 组合起来。
 * 此文件只模拟 NES 侧的状态；ESP32 的屏幕与音频输出由上层回调衔接。
 */
static nes_t nes;

nes_t *nes_getptr(void)
{
    return &nes;
}

/* 推进到当前帧结束；draw 只影响像素输出，不停止 CPU、mapper 和 APU 的执行。 */
void nes_emulate(bool draw)
{
    draw = draw && nes.vidbuf != NULL;

    while (nes.scanline < nes.scanlines_per_frame)
    {
        // 上游采用先执行部分 CPU 周期再绘制扫描线的顺序，以兼容 Battletoads。
        int elapsed_cycles = nes6502_execute(86 - 12);

        ppu_renderline(nes.vidbuf, nes.scanline, draw);

        if (nes.scanline == 241)
        {
            // 第 241 行进入垂直消隐附近，按 PPU 控制位向 CPU 请求 NMI。
            elapsed_cycles += nes6502_execute(6);
            if (nes.ppu->ctrl0 & PPU_CTRL0F_NMI)
                nes6502_nmi();

            if (nes.mapper->vblank)
                nes.mapper->vblank(&nes);
        }

        if (nes.mapper->hblank)
        {
            // 不同 mapper 检测行消隐的方式各异；上游在约 86 个 CPU 周期处调用。
            // 这是兼容性折中，不能将该实现理解为逐 PPU 时钟的精确硬件仿真。
            elapsed_cycles += nes6502_execute(86 - elapsed_cycles);
            nes.mapper->hblank(&nes);
        }

        // 补足本扫描线的 CPU 周期预算，并用实际消耗周期推进 APU 帧计数器。
        // 指令不能执行半条，剩余周期差保留到下一条扫描线继续平衡。
        nes.cycles += nes.cycles_per_scanline;

        elapsed_cycles += nes6502_execute(nes.cycles - elapsed_cycles);
        apu_fc_advance(elapsed_cycles);
        nes.cycles -= elapsed_cycles;

        ppu_endline();
        nes.scanline++;
    }

    nes.scanline = 0;

    // 一帧结束才交付画面；随后混音，供上层提交 samples_per_frame 个音频帧。
    if (draw && nes.blit_func)
        nes.blit_func(nes.vidbuf);

    apu_emulate();
}

uint8 *nes_setvidbuf(uint8 *vidbuf)
{
    uint8 *prevbuf = nes.vidbuf;
    nes.vidbuf = vidbuf;
    return prevbuf;
}

/* 配置每 period 个 CPU 周期触发的定时回调；该定时机制并不精确。 */
void nes_settimer(nes_timer_t *func, int period)
{
    nes.timer_func = func;
    nes.timer_period = period;
}

/* 连接解析好的卡带：查找 mapper，决定 PAL/NTSC 时序，随后复位主机。 */
int nes_insertcart(rom_t *cart)
{
    int status = 0;

    /* 保存卡带上下文，ROM 数据已由 rom_loadfile / rom_loadmem 准备。 */
    nes.cart = cart;
    if (NULL == nes.cart)
    {
        status = -1;
        goto _fail;
    }

    /* mapper 决定 CPU/PPU 地址窗口映射到哪一段卡带内存。 */
    nes.mapper = mmc_init(nes.cart);
    if (NULL == nes.mapper)
    {
        status = -2;
        goto _fail;
    }

    /* 自动模式采用 ROM 解析或游戏数据库提供的主机制式。 */
    if (nes.system == SYS_DETECT && nes.cart->system != SYS_UNKNOWN)
    {
        nes.system = nes.cart->system;
    }

    if (nes.system == SYS_UNKNOWN)
        MESSAGE_INFO("NES: System type: UNKNOWN  (NTSC-U)\n");
    else if (nes.system == SYS_NES_NTSC)
        MESSAGE_INFO("NES: System type: NES-NTSC (NTSC-U)\n");
    else if (nes.system == SYS_NES_PAL)
        MESSAGE_INFO("NES: System type: NES-PAL  (PAL)\n");
    else if (nes.system == SYS_FAMICOM)
        MESSAGE_INFO("NES: System type: FAMICOM  (NTSC-J)\n");

    /* 按制式选择时钟、帧率与扫描线数（参考 https://wiki.nesdev.com/w/index.php/Cycle_reference_chart）。 */
    if (nes.system == SYS_NES_PAL)
    {
        nes.cpu_clock = NES_CPU_CLOCK_PAL;
        nes.refresh_rate = NES_REFRESH_RATE_PAL;
        nes.scanlines_per_frame = NES_SCANLINES_PAL;
        nes.cycles_per_scanline = nes.cpu_clock / nes.refresh_rate / nes.scanlines_per_frame;
        nes.overscan = 0;
    }
    else
    {
        nes.cpu_clock = NES_CPU_CLOCK_NTSC;
        nes.refresh_rate = NES_REFRESH_RATE_NTSC;
        nes.scanlines_per_frame = NES_SCANLINES_NTSC;
        nes.cycles_per_scanline = nes.cpu_clock / nes.refresh_rate / nes.scanlines_per_frame;
        nes.overscan = 8;
    }

    /* Load BIOS file if required (currently only for Famicom Disk System) */
    if (nes.cart->type == ROM_TYPE_FDS)
    {
        if (nes.fds_bios == NULL)
        {
            // TO DO: Try biosfile = dirname(filename) / disksys.rom
            status = -3;
            goto _fail;
        }
        FILE *fp = fopen(nes.fds_bios, "rb");
        if (!fp || !fread(nes.cart->prg_rom, ROM_PRG_BANK_SIZE, nes.cart->prg_rom_banks, fp))
        {
            MESSAGE_ERROR("NES: BIOS file load failed from '%s'.\n", nes.fds_bios);
            status = -3;
            fclose(fp);
            goto _fail;
        }
        fclose(fp);
        MESSAGE_INFO("NES: BIOS file loaded from '%s'.\n", nes.fds_bios);
    }

    nes_reset(true);

    return status;

_fail:
    nes_shutdown();
    return status;
}

/* Load a ROM file */
int nes_loadfile(const char *filename)
{
    return nes_insertcart(rom_loadfile(filename));
}

/* 硬复位额外清空卡带 RAM；软硬复位都会重置各模拟部件与扫描线调度状态。 */
void nes_reset(bool hard_reset)
{
    if (hard_reset)
    {
        if (nes.cart->chr_ram_banks > 0)
            memset(nes.cart->chr_ram, 0, nes.cart->chr_ram_banks * ROM_CHR_BANK_SIZE);
        if (nes.cart->prg_ram_banks > 0)
            memset(nes.cart->prg_ram, 0, nes.cart->prg_ram_banks * ROM_PRG_BANK_SIZE);
    }

    apu_reset();
    ppu_reset();
    mem_reset();
    mmc_reset();
    input_reset();
    nes6502_reset();

    nes.vidbuf = NULL;
    nes.scanline = 241;
    nes.cycles = 0;

    /* if we're using VRAM, let the PPU know */
    nes.ppu->vram_present = (nes.cart->chr_rom_banks == 0);

    MESSAGE_INFO("NES: System reset (%s)\n", hard_reset ? "hard" : "soft");
}

/* Shutdown NES */
void nes_shutdown(void)
{
    mmc_shutdown();
    mem_shutdown();
    ppu_shutdown();
    apu_shutdown();
    nes6502_shutdown();
    rom_free();
}

/* 初始化各部件并相互连接；CPU 访问的页表来自 mem，卡带稍后才插入。 */
nes_t *nes_init(nes_type_t system, int sample_rate, bool stereo, const char *fds_bios)
{
    memset(&nes, 0, sizeof(nes_t));

    nes.system = system;
    nes.refresh_rate = 60;

    /* memory */
    nes.mem = mem_init_();
    if (NULL == nes.mem)
        goto _fail;

    /* cpu */
    nes.cpu = nes6502_init(nes.mem->pages);
    if (NULL == nes.cpu)
        goto _fail;

    /* ppu */
    nes.ppu = ppu_init();
    if (NULL == nes.ppu)
        goto _fail;

    /* apu */
    nes.apu = apu_init(sample_rate, stereo);
    if (NULL == nes.apu)
        goto _fail;

    /* input */
    nes.input = input_init();
    if (NULL == nes.input)
        goto _fail;

    /* fds */
    if (fds_bios)
        nes.fds_bios = strdup(fds_bios);

    MESSAGE_INFO("NES: System initialized!\n");
    return &nes;

_fail:
    MESSAGE_ERROR("NES: System initialized failed!\n");
    nes_shutdown();
    return NULL;
}
