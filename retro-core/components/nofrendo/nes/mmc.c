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
** nes/mmc.c: Mapper emulation
**
*/

#include "mappers/mappers.h"
#include "nes.h"

/* mapper 模拟卡带上的地址映射电路，支持比 CPU/PPU 地址窗口更大的 ROM。
 * 切换 bank 的核心操作是重定向页指针，不是反复从 TF 卡读取或搬运整个 ROM。
 */
static mapper_t mapper;
static rom_t *cart;

/* 将指定大小的 PRG bank 映射到 CPU 地址窗口；size 单位为 KB，bank 为块序号。 */
void mmc_bankprg(unsigned size, unsigned address, int bank, uint8 *base)
{
   // ASSERT(size == 8 || size == 16 || size == 32);
   int banks = 16;

   // if (base == PRG_ANY)
   //    base = cart->prg_rom_banks ? PRG_ROM : PRG_RAM;

   if (base == PRG_ROM)
   {
      base = cart->prg_rom;
      banks = cart->prg_rom_banks;
   }
   else if (base == PRG_RAM)
   {
      base = cart->prg_ram;
      banks = cart->prg_ram_banks;
   }

   banks /= (size / 8);

   // 负数 bank 从末尾倒数，常用于把最后一个程序块固定到复位向量所在窗口。
   if (bank < 0)
      bank += banks;

   if (base == NULL || banks == 0 || bank < 0) // || bank > banks)
   {
      MESSAGE_ERROR("MMC: Bogus PRG mapping! Address: $%04X, Size: %dKB, Bank: %d, Banks: %d, Base: %p\n",
         address, size, bank, banks, base);
      return;
   }

   bank %= banks;
   base += bank * (size * 1024);

   for (size_t i = 0, num = (size * 1024 / MEM_PAGESIZE); i < num; ++i)
   {
      mem_setpage((address >> MEM_PAGESHIFT) + i, &base[i * MEM_PAGESIZE]);
   }
}

/* 将图案数据映射到 PPU 地址空间；CHR_ANY 按卡带是否有 CHR-ROM 自动选择来源。 */
void mmc_bankchr(unsigned size, unsigned address, int bank, uint8 *base)
{
   // ASSERT(size == 1 || size == 2 || size == 4 || size == 8);
   int banks = 128;

   if (base == CHR_ANY)
      base = cart->chr_rom_banks ? CHR_ROM : CHR_RAM;

   if (base == CHR_ROM)
   {
      base = cart->chr_rom;
      banks = cart->chr_rom_banks;
   }
   else if (base == CHR_RAM)
   {
      base = cart->chr_ram;
      banks = cart->chr_ram_banks;
   }

   banks *= (8 / size);

   if (bank < 0)
      bank += banks;

   if (base == NULL || banks == 0 || bank < 0) // || bank > banks)
   {
      MESSAGE_ERROR("MMC: Bogus CHR mapping! Address: $%04X, Size: %dKB, Bank: %d, Banks: %d, Base: %p\n",
         address, size, bank, banks, base);
      return;
   }

   bank %= banks;
   base += bank * (size * 1024);

   for (size_t i = 0; i < size; ++i)
   {
      ppu_setpage((address >> PPU_PAGESHIFT) + i, &base[i * PPU_PAGESIZE]);
   }
}

/* 先建立通用映射，再由具体 mapper 的初始化函数覆盖其特有的窗口与回调。 */
void mmc_reset(void)
{
   /* 可写卡带 RAM 的默认窗口从 CPU $6000 开始。 */
   if (cart->prg_ram_banks > 0)
      mmc_bankprg(8, 0x6000, 0, PRG_RAM);

   /* 默认把程序首块和末块放入 $8000/$C000 窗口，具体 mapper 可重新安排。 */
   if (cart->prg_rom_banks > 1)
   {
      mmc_bankprg(16, 0x8000,  0, PRG_ROM);
      mmc_bankprg(16, 0xC000, -1, PRG_ROM);
   }
   else if (cart->prg_rom_banks == 1)
   {
      mmc_bankprg(8, 0xE000,  0, PRG_ROM);
   }

   /* PPU $0000 起的 8KB 区域用作图案表。 */
   mmc_bankchr(8, 0x0000, 0, CHR_ANY);

   /* 名称表镜像决定逻辑背景表怎样复用实际显存。 */
   ppu_setmirroring(cart->mirroring);

   /* 清除上一卡带可能注册的特殊回调及扩展音源。 */
   ppu_setlatchfunc(NULL);
   ppu_setvreadfunc(NULL);
   nes_settimer(NULL, 0);
   apu_setext(NULL);

   /* 按具体卡带电路重新配置；初始化可能覆盖上面建立的通用映射。 */
   if (mapper.init)
      mapper.init(cart);
}

void mmc_shutdown()
{
   //
}

mapper_t *mmc_init(rom_t *_cart)
{
   // 在已编译的 mapper 表中按编号查找；未实现的编号会返回 NULL 给上层报错。
   int mapper_number = _cart->mapper_number;

   mapper.number = -1;

   for (int i = 0; mappers[i]; i++)
   {
      if (mappers[i]->number == mapper_number)
      {
         mapper = *mappers[i]; // 复制该 mapper 的函数表与描述。
         cart = _cart;

         MESSAGE_INFO("MMC: Mapper %s (iNES %03d)\n", mapper.name, mapper.number);
         MESSAGE_INFO("MMC: PRG-ROM: %d banks\n", cart->prg_rom_banks);
         MESSAGE_INFO("MMC: PRG-RAM: %d banks\n", cart->prg_ram_banks);
         if (cart->chr_rom_banks > 0)
            MESSAGE_INFO("MMC: CHR-ROM: %d banks\n", cart->chr_rom_banks);
         else
            MESSAGE_INFO("MMC: CHR-RAM: %d banks\n", cart->chr_ram_banks);

         return &mapper;
      }
   }

   MESSAGE_ERROR("MMC: Unsupported mapper %03d\n", mapper_number);
   return NULL;
}
