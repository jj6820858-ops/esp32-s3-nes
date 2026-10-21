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
** nes/state.c: Save state support
**
*/

#include "nes.h"

/**
 * 即时存档格式：保存模拟主机的执行现场，使恢复后能从保存位置继续运行。
 * 这与只保存游戏电池 RAM 的 rom_savesram 不同，也不是 ROM 游戏文件本身。
 *
 * 文件由 8 字节头和后续若干数据块组成；整数使用大端字节序。
 *
 *    文件头：
 *       标识（4 字节），固定为 "SNSS"
 *       数据块数量（4 字节），int32be
 *
 *    数据块：
 *       块名（4 字节）
 *       块版本（4 字节），int32be
 *       块长度（4 字节），int32be
 *       数据（长度由块长度字段指定）
 *
 * 当前写入块的版本均为 1；实际大小以 state_save 写出的长度字段为准。
 * 块名和内容（不含 12 字节块头）：
 * - BASR：CPU 寄存器、内存和 PPU 状态，6449 字节
 * - INFO：信息区，256 字节
 * - SOUN：声音寄存器，22 字节
 * - SRAM：PRG-RAM 加 1 字节使能标志
 * - VRAM：CHR-RAM 数据
 * - MPRD：卡带映射状态，当前代码写入 0x218 字节
 */

typedef struct
{
   uint8  magic[4];
   uint32 blocks;
} header_t;

typedef struct
{
   uint8  name[4];
   uint32 version;
   uint32 length;
   uint8  data[];
} block_t;

#define _fread(buffer, size) {                       \
   if (fread(buffer, size, 1, file) != 1)            \
   {                                                 \
      MESSAGE_ERROR("state_load: fread failed.\n");  \
      goto _error;                                   \
   }                                                 \
}

#define _fwrite(buffer, size) {                      \
   if (fwrite(buffer, size, 1, file) != 1)           \
   {                                                 \
      MESSAGE_ERROR("state_save: fwrite failed.\n"); \
      goto _error;                                   \
   }                                                 \
}

#ifndef IS_BIG_ENDIAN
   static inline uint16 swap16(uint16 x)
   {
      return (x << 8) | (x >> 8);
   }

   static inline uint32 swap32(uint32 x)
   {
      return ((x>>24)&0xff) | ((x<<8)&0xff0000) | ((x>>8)&0xff00) | ((x<<24)&0xff000000);
   }
#else
   #define swap16(x) (x)
   #define swap32(x) (x)
#endif


static bool memory_zone_dirty(const void *ptr, size_t size)
{
   size_t pos = 0;

   while (size - pos >= 4)
   {
      if (*((uint32 *)(ptr + pos)) != 0)
         return true;
      pos += 4;
   }

   while (pos < size)
   {
      if (*((uint8 *)(ptr + pos)) != 0)
         return true;
      pos += 1;
   }

   return false;
}


int state_save(const char* fn)
{
   // 分块保存 CPU 寄存器、CPU RAM、PPU 内存/寄存器、声音寄存器与卡带状态。
   uint32 numberOfBlocks = 0;
   uint8 buffer[600];
   nes_t *machine = nes_getptr();
   FILE *file;

   if (!(file = fopen(fn, "wb")))
   {
       MESSAGE_ERROR("state_save: file '%s' could not be opened.\n", fn);
       return -1; //goto _error;
   }

   MESSAGE_INFO("state_save: file '%s' opened.\n", fn);

   _fwrite("SNSS\x00\x00\x00\x05", 8);


   /****************************************************/

   MESSAGE_INFO("  - Saving base block\n");

   buffer[0] = machine->cpu->a_reg;
   buffer[1] = machine->cpu->x_reg;
   buffer[2] = machine->cpu->y_reg;
   buffer[3] = machine->cpu->p_reg;
   buffer[4] = machine->cpu->s_reg;
   buffer[5] = machine->cpu->pc_reg / 256;
   buffer[6] = machine->cpu->pc_reg % 256;
   buffer[7] = machine->ppu->ctrl0;
   buffer[8] = machine->ppu->ctrl1;

   _fwrite("BASR\x00\x00\x00\x01\x00\x00\x19\x31", 12);
   _fwrite(&buffer, 9);
   _fwrite(machine->mem->ram, 0x800);
   _fwrite(machine->ppu->oam, 0x100);
   _fwrite(machine->ppu->nametab, 0x1000);

   /* 色板仅保存 NES 色号，去掉本模拟器内部用于绘制优先级的标记位。 */
   for (int i = 0; i < 32; i++)
      buffer[i] = machine->ppu->palette[i] & 0x3F;

   buffer[32] = machine->ppu->nt_map[0];
   buffer[33] = machine->ppu->nt_map[1];
   buffer[34] = machine->ppu->nt_map[2];
   buffer[35] = machine->ppu->nt_map[3];
   buffer[36] = machine->ppu->vaddr / 256;
   buffer[37] = machine->ppu->vaddr % 256;
   buffer[38] = machine->ppu->oam_addr;
   buffer[39] = machine->ppu->tile_xofs;

   _fwrite(&buffer, 40);
   numberOfBlocks++;


   /****************************************************/

   MESSAGE_INFO("  - Saving info block\n");

   _fwrite("INFO\x00\x00\x00\x01\x00\x00\x01\x00", 12);
   _fwrite(&buffer, 0x100);
   numberOfBlocks++;


   /****************************************************/

   MESSAGE_INFO("  - Saving sound block\n");

   buffer[0x00] = machine->apu->rectangle[0].regs[0];
   buffer[0x01] = machine->apu->rectangle[0].regs[1];
   buffer[0x02] = machine->apu->rectangle[0].regs[2];
   buffer[0x03] = machine->apu->rectangle[0].regs[3];
   buffer[0x04] = machine->apu->rectangle[1].regs[0];
   buffer[0x05] = machine->apu->rectangle[1].regs[1];
   buffer[0x06] = machine->apu->rectangle[1].regs[2];
   buffer[0x07] = machine->apu->rectangle[1].regs[3];
   buffer[0x08] = machine->apu->triangle.regs[0];
   buffer[0x0A] = machine->apu->triangle.regs[1];
   buffer[0x0B] = machine->apu->triangle.regs[2];
   buffer[0X0C] = machine->apu->noise.regs[0];
   buffer[0X0E] = machine->apu->noise.regs[1];
   buffer[0x0F] = machine->apu->noise.regs[2];
   buffer[0x10] = machine->apu->dmc.regs[0];
   buffer[0x11] = machine->apu->dmc.regs[1];
   buffer[0x12] = machine->apu->dmc.regs[2];
   buffer[0x13] = machine->apu->dmc.regs[3];
   buffer[0x15] = machine->apu->control_reg;

   _fwrite("SOUN\x00\x00\x00\x01\x00\x00\x00\x16", 12);
   _fwrite(&buffer, 0x16);
   numberOfBlocks++;


   /****************************************************/

   if (memory_zone_dirty(machine->cart->chr_ram, 0x2000 * machine->cart->chr_ram_banks))
   {
      MESSAGE_INFO("  - Saving VRAM block\n");

      _fwrite("VRAM\x00\x00\x00\x01\x00\x00\x20\x00", 12);
      _fwrite(machine->cart->chr_ram, 0x2000 * machine->cart->chr_ram_banks);
      numberOfBlocks++;
   }


   /****************************************************/

   if (memory_zone_dirty(machine->cart->prg_ram, 0x2000 * machine->cart->prg_ram_banks))
   {
      MESSAGE_INFO("  - Saving SRAM block\n");

      // 第 0 字节为 SRAM 使能标志（当前未使用），后续为实际 PRG-RAM 数据。
      // 上游这里的长度字段固定写为 $2001。
      _fwrite("SRAM\x00\x00\x00\x01\x00\x00\x20\x01\x01", 13);
      _fwrite(machine->cart->prg_ram, 0x2000 * machine->cart->prg_ram_banks);
      numberOfBlocks++;
   }


   /****************************************************/

   if (machine->mapper->number > 0)
   {
      // 同一份 ROM 在不同时刻的 bank 映射可能不同，因此也要保存页映射与私有寄存器。
      MESSAGE_INFO("  - Saving mapper block\n");

      memset(buffer, 0, sizeof(buffer));

      for (int i = 0; i < 4; i++)
      {
         uint16 temp = (mem_getpage((i + 4) * 4) - machine->cart->prg_rom) >> 13;
         temp = swap16(temp);
         buffer[(i * 2) + 0] = ((uint8 *) &temp)[0];
         buffer[(i * 2) + 1] = ((uint8 *) &temp)[1];
      }

      for (int i = 0; i < 8; i++)
      {
         uint16 temp = (machine->cart->chr_rom_banks) ? ((ppu_getpage(i) - machine->cart->chr_rom) >> 10) : (i);
         temp = swap16(temp);
         buffer[8 + (i * 2) + 0] = ((uint8 *) &temp)[0];
         buffer[8 + (i * 2) + 1] = ((uint8 *) &temp)[1];
      }

      if (machine->mapper->get_state)
      {
         machine->mapper->get_state(buffer + 0x18);
      }

      _fwrite("MPRD\x00\x00\x00\x01\x00\x00\x02\x18", 12);
      _fwrite(&buffer, 0x218);
      numberOfBlocks++;
   }


   /****************************************************/

   // RAM 全零时可省略相应块，因此写完后回到文件头填入实际块数。
   fseek(file, 4, SEEK_SET);
   numberOfBlocks = swap32(numberOfBlocks);
   _fwrite(&numberOfBlocks, 4);

   fclose(file);

   MESSAGE_INFO("state_save: Game saved!\n");

   return 0;

_error:
   MESSAGE_ERROR("state_save: Save failed!\n");
   fclose(file);
   return -1;
}


int state_load(const char* fn)
{
   // 按块名分发恢复过程；每块的长度字段确定下一块位置，不依赖固定块顺序。
   uint8 buffer[600];

   nes_t *machine = nes_getptr();
   FILE *file;

   if (!(file = fopen(fn, "rb")))
   {
       MESSAGE_ERROR("state_load: file '%s' could not be opened.\n", fn);
       return -1; //goto _error;
  }

   _fread(buffer, 8);

   if (memcmp(buffer, "SNSS", 4) != 0)
   {
      MESSAGE_ERROR("state_load: file '%s' is not a save file.\n", fn);
      goto _error;
   }

   uint32 numberOfBlocks = swap32(*((uint32*)&buffer[4]));
   uint32 nextBlock = 8;

   MESSAGE_INFO("state_load: file '%s' opened, blocks=%u.\n", fn, numberOfBlocks);

   for (uint32 blk = 0; blk < numberOfBlocks; blk++)
   {
      fseek(file, nextBlock, SEEK_SET);
      _fread(buffer, 12);

      uint32 blockVersion = swap32(*((uint32*)&buffer[4]));
      uint32 blockLength = swap32(*((uint32*)&buffer[8]));

      UNUSED(blockVersion);

      nextBlock += 12 + blockLength;

      /****************************************************/

      if (memcmp(buffer, "BASR", 4) == 0)
      {
         MESSAGE_INFO("  - Found base block (%u bytes)\n", blockLength);

         _fread(buffer, 9);

         machine->cpu->a_reg = buffer[0x0];
         machine->cpu->x_reg = buffer[0x1];
         machine->cpu->y_reg = buffer[0x2];
         machine->cpu->p_reg = buffer[0x3];
         machine->cpu->s_reg = buffer[0x4];
         machine->cpu->pc_reg = swap16(*((uint16*)&buffer[0x5]));
         machine->ppu->ctrl0 = buffer[0x7];
         machine->ppu->ctrl1 = buffer[0x8];

         _fread(machine->mem->ram, 0x800);
         _fread(machine->ppu->oam, 0x100);
         _fread(machine->ppu->nametab, 0x1000);
         _fread(machine->ppu->palette, 0x20);

         /* 补回模拟器内部的背景透明标记，以兼容当前精灵优先级绘制方式。 */
         for (int i = 0; i < 8; i++)
            machine->ppu->palette[i << 2] = machine->ppu->palette[0] | 0x80; // BG_TRANS;

         _fread(buffer, 8);

         machine->ppu->vaddr = swap16(*((uint16*)&buffer[0x4]));
         machine->ppu->oam_addr = buffer[0x6];
         machine->ppu->tile_xofs = buffer[0x7];

         /* 除复制寄存器值，还需重建 PPU 的写入状态、名称表映射和内部派生状态。 */
         machine->ppu->flipflop = 0;
         machine->ppu->strikeflag = false;

         ppu_setnametable(0, buffer[0]);
         ppu_setnametable(1, buffer[1]);
         ppu_setnametable(2, buffer[2]);
         ppu_setnametable(3, buffer[3]);

         ppu_write(PPU_CTRL0, machine->ppu->ctrl0);
         ppu_write(PPU_CTRL1, machine->ppu->ctrl1);
         ppu_write(PPU_VADDR, machine->ppu->vaddr >> 8);
         ppu_write(PPU_VADDR, machine->ppu->vaddr & 0xFF);
      }


      /****************************************************/

      else if (memcmp(buffer, "VRAM", 4) == 0)
      {
         MESSAGE_INFO("  - Found VRAM block (%u bytes)\n", blockLength);

         if (machine->cart->chr_ram_banks < (blockLength / ROM_CHR_BANK_SIZE))
         {
            MESSAGE_ERROR("Invalid block size!\n");
            continue;
         }

         _fread(machine->cart->chr_ram, blockLength);
      }


      /****************************************************/

      else if (memcmp(buffer, "SRAM", 4) == 0)
      {
         MESSAGE_INFO("  - Found SRAM block (%u bytes)\n", blockLength);

         if (machine->cart->prg_ram_banks < ((blockLength-1) / ROM_PRG_BANK_SIZE))
         {
            MESSAGE_ERROR("Invalid block size!\n");
            continue;
         }

         _fread(buffer, 1); // 读过 SRAM 使能标志，当前保存时始终为真。
         _fread(machine->cart->prg_ram, blockLength - 1);
      }


      /****************************************************/

      else if (memcmp(buffer, "MPRD", 4) == 0)
      {
         // 先恢复 CPU/PPU 页映射，再交给具体 mapper 恢复其私有寄存器。
         MESSAGE_INFO("  - Found mapper block (%u bytes)\n", blockLength);

         _fread(buffer, MIN(blockLength, sizeof(buffer)));

         for (int i = 0; i < 4; i++)
            mmc_bankprg(8, 0x8000 + (i * 0x2000), swap16(((uint16*)buffer)[i]), PRG_ROM);

         if (machine->cart->chr_rom_banks)
         {
            for (int i = 0; i < 8; i++)
               mmc_bankchr(1, i * 0x400, swap16(((uint16*)buffer)[4 + i]), CHR_ROM);
         }
         else if (machine->cart->chr_ram)
         {
            for (int i = 0; i < 8; i++)
               mmc_bankchr(1, i * 0x400, i, CHR_RAM);
         }

         if (machine->mapper->set_state)
            machine->mapper->set_state(buffer + 0x18);
      }


      /****************************************************/

      else if (memcmp(buffer, "SOUN", 4) == 0)
      {
         // 复位 APU 后重放寄存器写入；当前格式保存的是这些寄存器，而非全部混音内部量。
         MESSAGE_INFO("  - Found sound block (%u bytes)\n", blockLength);

         _fread(buffer, 0x16);

         apu_reset();

         for (int i = 0; i < 0x16; i++)
            apu_write(0x4000 + i, buffer[i]);
      }


      /****************************************************/

      else if (memcmp(buffer, "INFO", 4) == 0)
      {
         MESSAGE_INFO("  - Found info block (%u bytes)\n", blockLength);

         _fread(buffer, 0x100);

         // 当前读取 INFO 后不使用其内容，保留上游格式兼容性。
      }


      /****************************************************/

      else
      {
         MESSAGE_ERROR("Found unknown block type!\n");
      }
   }

   /* 全部块处理完成后关闭文件。 */
   fclose(file);

   MESSAGE_INFO("state_load: Game restored\n");

   return 0;

_error:
   MESSAGE_ERROR("state_load: Load failed!\n");
   fclose(file);
   return -1;
}
