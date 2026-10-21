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
** nes/rom.c: NES file loader (ROM, FDS, NSF, SAV, etc)
**
*/

#include <stdio.h>
#include "nes.h"
#include "../database.h"

static rom_t rom;

/* 卡带电池 RAM 存档：只保存 PRG-RAM；完整 CPU/PPU 现场由 state.c 即时存档处理。 */
void rom_savesram(const char *filename)
{
   if (!rom.battery || rom.prg_ram_banks < 1)
   {
      MESSAGE_ERROR("ROM: Game has no battery-backed SRAM!\n");
      return;
   }
   FILE *fp = fopen(filename, "wb");
   if (fp)
   {
      fwrite(rom.prg_ram, ROM_PRG_BANK_SIZE, rom.prg_ram_banks, fp);
      fclose(fp);
      MESSAGE_INFO("ROM: Wrote battery RAM to %s.\n", filename);
   }
}

/* 从文件恢复卡带电池 RAM。 */
void rom_loadsram(const char *filename)
{
   if (!rom.battery || rom.prg_ram_banks < 1)
   {
      MESSAGE_ERROR("ROM: Game has no battery-backed SRAM!\n");
      return;
   }
   FILE *fp = fopen(filename, "rb");
   if (fp)
   {
      fread(rom.prg_ram, ROM_PRG_BANK_SIZE, rom.prg_ram_banks, fp);
      fclose(fp);
      MESSAGE_INFO("ROM: Read battery RAM from %s.\n", filename);
   }
}

/* 解析内存中的 ROM，建立数据指针与容量描述；不在这里执行游戏指令。
 * iNES 常见布局：16 字节头 → 可选 512 字节 trainer → PRG 程序 → CHR 图案。
 * mapper 是卡带映射电路编号，PRG/CHR 则分别供 CPU 和 PPU 侧使用。
 */
rom_t *rom_loadmem(uint8 *data, size_t size)
{
   if (!data || size < 16)
      return NULL;

   rom = (rom_t) {
      .type = ROM_TYPE_INVALID,
      .mapper_number = 0,
      .mirroring = PPU_MIRROR_HORI,
      .system = SYS_UNKNOWN,
      .data_ptr = data,
      .data_len = size,
   };

   if (!memcmp(data, ROM_NES_MAGIC, 4))
   {
      inesheader_t *header = (inesheader_t *)data;

      MESSAGE_INFO("ROM: Found iNES file of size %d.\n", (int)size);

      rom.type = ROM_TYPE_INES;
      rom.mapper_number = ((header->mapper_hinybble & 0xF0) | (header->rom_type >> 4));
      // https://wiki.nesdev.com/w/index.php/INES
      // 上游兼容旧文件头：保留区非零时忽略 mapper 的高 4 位，避免旧工具污染头部。
      // 这是当前加载器的实际处理，不能据此认为它完整支持 NES 2.0 文件头。
      if (header->reserved2 != 0)
         rom.mapper_number &= 0x0F;
      rom.battery = (header->rom_type & ROM_FLAG_BATTERY);
      rom.fourscreen = (header->rom_type & ROM_FLAG_FOURSCREEN);
      rom.vertical = (header->rom_type & ROM_FLAG_VERTICAL);
      rom.trainer = (header->rom_type & ROM_FLAG_TRAINER) ? data + 0x10 : NULL;
      // iNES 的 PRG 单位为 16KB；本核心统一按 8KB 计数，因此这里乘 2。
      rom.prg_rom_banks = header->prg_banks * 2;
      rom.chr_rom_banks = header->chr_banks;
      rom.prg_ram_banks = 1; // 上游默认分配 8KB；此处未从 iNES 头读取 RAM 容量。
      rom.chr_ram_banks = 1; // 同样预留 8KB CHR-RAM，供无 CHR-ROM 的卡带等情况使用。

      if (rom.fourscreen)
         rom.mirroring = PPU_MIRROR_FOUR;
      else if (rom.vertical)
         rom.mirroring = PPU_MIRROR_VERT;

      size_t data_offset = rom.trainer ? 0x210 : 0x010;

      rom.checksum = CRC32(0, rom.data_ptr + data_offset, rom.data_len - data_offset);

      // 按去掉文件头和 trainer 后的数据 CRC 查表，可修正 mapper、镜像方式和制式。
      const db_game_t *entry = games_database;
      while (entry->crc && entry->crc != rom.checksum)
         entry++;

      if (entry->crc && entry->crc == rom.checksum)
      {
         MESSAGE_INFO("ROM: Game found in database.\n");

         rom.system = entry->system;

         if (entry->mapper != rom.mapper_number)
         {
            MESSAGE_WARN("ROM: mapper mismatch! (DB: %d, ROM: %d)\n", entry->mapper, rom.mapper_number);
            rom.mapper_number = entry->mapper;
         }

         if (entry->mirror != rom.mirroring)
         {
            MESSAGE_WARN("ROM: mirroring mismatch! (DB: %d, ROM: %d)\n", entry->mirror, rom.mirroring);
            rom.mirroring = entry->mirror;
         }

         if (entry->prg_rom != rom.prg_rom_banks)
         {
            MESSAGE_WARN("ROM: prg_rom_banks mismatch! (DB: %d, ROM: %d)\n", entry->prg_rom, rom.prg_rom_banks);
            // rom.prg_rom_banks = entry->prg_rom;
         }

         if (entry->prg_ram != rom.prg_ram_banks)
         {
            MESSAGE_WARN("ROM: prg_ram_banks mismatch! (DB: %d, ROM: %d)\n", entry->prg_ram, rom.prg_ram_banks);
            // rom.prg_ram_banks = entry->prg_ram;
         }

         if (entry->chr_rom > -1 && entry->chr_rom != rom.chr_rom_banks)
         {
            MESSAGE_WARN("ROM: chr_rom_banks mismatch! (DB: %d, ROM: %d)\n", entry->chr_rom, rom.chr_rom_banks);
            // rom.chr_rom_banks = entry->chr_rom;
         }

         if (entry->chr_ram > -1 && entry->chr_ram != rom.chr_ram_banks)
         {
            MESSAGE_WARN("ROM: chr_ram_banks mismatch! (DB: %d, ROM: %d)\n", entry->chr_ram, rom.chr_ram_banks);
            // rom.chr_ram_banks = entry->chr_ram;
         }
      }
      else
      {
         MESSAGE_INFO("ROM: Game not found in database.\n");
      }

      // 上游这里只对过短文件打印错误，下面的 return 仍被注释；并非严格拒绝校验。
      size_t expected_size = data_offset + (rom.prg_rom_banks * ROM_PRG_BANK_SIZE) + (rom.chr_rom_banks * ROM_CHR_BANK_SIZE);
      if (size < expected_size)
      {
         MESSAGE_ERROR("ROM: Expected rom size to be at least %d bytes, got %d.\n", expected_size, size);
         // return NULL;
      }

      rom.prg_rom = data + data_offset;
      if (rom.chr_rom_banks > 0)
         rom.chr_rom = data + data_offset + (rom.prg_rom_banks * ROM_PRG_BANK_SIZE);
   }
   else if (!memcmp(data, ROM_FDS_MAGIC, 4) || !memcmp(data, ROM_FDS_RAW_MAGIC, 15))
   {
      MESSAGE_INFO("ROM: Found FDS file of size %d.\n", (int)size);

      rom.type = ROM_TYPE_FDS;
      rom.mapper_number = 20;
      rom.system = SYS_FAMICOM;
      rom.battery = true;
      rom.prg_ram_banks = 4; // The FDS adapter contains 32KB to store game program
      rom.chr_ram_banks = 1; // The FDS adapter contains 8KB
      rom.prg_rom_banks = 1; // This will contain the FDS BIOS
      rom.checksum = CRC32(0, rom.data_ptr, rom.data_len);
   }
   else if (!memcmp(data, ROM_NSF_MAGIC, 5))
   {
      MESSAGE_INFO("ROM: Found NSF file of size %d.\n", (int)size);

      rom.type = ROM_TYPE_NSF;
      rom.mapper_number = 31;
      rom.prg_ram_banks = 1; // Some songs may need it. I store a bootstrap program there at the moment
      rom.chr_ram_banks = 1; // Not used but some code might assume it will be present...
      rom.prg_rom_banks = 4; // This is actually PRG-RAM but some of our code assumes PRG-ROM to be present...
      rom.checksum = CRC32(0, rom.data_ptr, rom.data_len);
   }
   else
   {
      MESSAGE_ERROR("ROM: File is not a valid NES image!\n");
      return NULL;
   }

   MESSAGE_INFO("ROM: CRC32:  %08X\n", rom.checksum);
   MESSAGE_INFO("ROM: Mapper: %d, PRG:%dK, CHR:%dK, Flags: %c%c%c%c\n",
               rom.mapper_number,
               rom.prg_rom_banks * 8, rom.chr_rom_banks * 8,
               (rom.vertical) ? 'V' : 'H',
               (rom.battery) ? 'B' : '-',
               (rom.trainer) ? 'T' : '-',
               (rom.fourscreen) ? '4' : '-');

   // ROM 通常直接指向已读入的文件缓冲；可写 RAM 单独分配，缺失的 PRG 区另分配。
   // malloc 的实际物理内存位置由平台堆配置决定，此处没有强制指定 PSRAM。
   rom.prg_ram = malloc(rom.prg_ram_banks * ROM_PRG_BANK_SIZE);
   rom.chr_ram = malloc(rom.chr_ram_banks * ROM_CHR_BANK_SIZE);
   if (!rom.prg_rom)
   {
      rom.prg_rom = malloc(rom.prg_rom_banks * ROM_PRG_BANK_SIZE);
      rom.free_prg_rom = true;
   }

   if (!rom.prg_ram || !rom.chr_ram || !rom.prg_rom)
   {
      MESSAGE_ERROR("ROM: Memory allocation failed!\n");
      rom_free();
      return NULL;
   }

   return &rom;
}

/* 文件入口：读取整份 ROM 到堆内存，再交给 rom_loadmem 解析；当前上限为 2MiB。 */
rom_t *rom_loadfile(const char *filename)
{
   uint8 *data = NULL;
   long size = 0;
   FILE *fp;

   if (!filename)
      return NULL;

   if ((fp = fopen(filename, "rb")))
   {
      MESSAGE_INFO("ROM: Loading file '%s'\n", filename);

      fseek(fp, 0, SEEK_END);
      size = ftell(fp);
      fseek(fp, 0, SEEK_SET);

      if (size < 16 || size > 0x200000)
      {
         MESSAGE_ERROR("ROM: File size error\n");
      }
      else if ((data = malloc(size)) == NULL)
      {
         MESSAGE_ERROR("ROM: Memory allocation failed\n");
      }
      else if (fread(data, size, 1, fp) != 1)
      {
         MESSAGE_ERROR("ROM: Read error\n");
         free(data);
         data = NULL;
      }
      fclose(fp);
   }

   // 先释放之前的卡带上下文；没有已载入卡带时，rom_free 不释放任何数据。
   rom_free();

   if (rom_loadmem(data, size) == NULL)
   {
      MESSAGE_ERROR("ROM: Load error\n");
      free(data);
      return NULL;
   }

   if (rom.system == SYS_UNKNOWN)
   {
      if (strstr(filename, "(E)")
         || strstr(filename, "(Europe)")
         || strstr(filename, "(A)")
         || strstr(filename, "(Australia)"))
         rom.system = SYS_NES_PAL;
   }
   // 记录缓冲的所有权，退出游戏时由 rom_free 释放本次文件读取分配的内存。
   rom.free_data_ptr = true;
   rom.filename = strdup(filename);
   return &rom;
}

/* 按所有权标志释放 ROM 数据，避免把指向同一文件缓冲的 PRG/CHR 切片重复释放。 */
void rom_free(void)
{
   if (rom.type != ROM_TYPE_INVALID)
   {
      if (rom.free_data_ptr)
         free(rom.data_ptr);
      if (rom.free_prg_rom)
         free(rom.prg_rom);
      if (rom.free_chr_rom)
         free(rom.chr_rom);
      if (rom.free_trainer)
         free(rom.trainer);
      free(rom.prg_ram);
      free(rom.chr_ram);
      free(rom.filename);
   }
   memset(&rom, 0, sizeof(rom_t));
}
