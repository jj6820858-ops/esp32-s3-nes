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
** nes/input.c: Input emulation (joypads)
**
*/

#include "nes.h"

/* 虚拟手柄端口：上层一次更新按键位图，游戏通过 CPU 地址 $4016/$4017 逐位读取。
 * 这里描述的是模拟器的端口协议，与本板 I2C 触摸芯片的通信协议分属两层。
 */
static input_t ports[2];
static int strobe = 0;


void input_write(uint32 address, uint8 value)
{
    // mem.c 将 CPU 对 $4016 的写入路由到这里；只取最低位作为 strobe。
    value &= 1;

    if (!value && strobe)
    {
        // 当前实现检测 1→0 后复位两个端口的读序号，从 A 键重新开始读取。
        ports[0].reads = 0;
        ports[1].reads = 0;
    }

    strobe = value;
}

uint8 input_read(uint32 address)
{
    /* $4016 对应 1 号手柄，$4017 对应 2 号手柄；地址最低位选择数组成员。 */
    input_t *port = &ports[address & 1];
    unsigned retval = 0;
    unsigned value = 0;

    if (port->type == NES_JOYPAD)
    {
        value = port->state;
    }

    /* 清除上/下、左/右同时按下的冲突组合。 */
    if ((value & NES_PAD_UP) && (value & NES_PAD_DOWN))
        value &= ~(NES_PAD_UP | NES_PAD_DOWN);

    if ((value & NES_PAD_LEFT) && (value & NES_PAD_RIGHT))
        value &= ~(NES_PAD_LEFT | NES_PAD_RIGHT);

    /* 读序号递增，最低位依次返回 A、B、SELECT、START、上、下、左、右。
     * 当前直接读取 port->state 位图，没有额外复制一份独立的锁存位图。
     * 上游保留返回值 bit6=1，以兼容其总线行为处理。
     */
    retval |= (0x40 | ((value >> port->reads++) & 1));

    if (address == 0x4017 && port->type == NES_ZAPPER) // 2 号端口也支持光枪状态。
    {
        retval |= port->state;
    }

    return retval;
}

void input_connect(int port, nes_dev_t type)
{
    ports[port & 1].type = type;
    ports[port & 1].reads = 0;
    ports[port & 1].state = 0;
}

void input_update(int port, int state)
{
    // 本板每帧把触摸转换后的 NES_PAD_* 组合写入 1 号端口，供游戏稍后串行读取。
    ports[port & 1].state = state;
}

input_t *input_init(void)
{
    input_connect(0, NES_JOYPAD);
    input_connect(1, NES_NOTHING);
    return (input_t *)ports;
}

void input_reset(void)
{
    ports[0].reads = ports[1].reads = 0;
    ports[0].state = ports[1].state = 0;
}
