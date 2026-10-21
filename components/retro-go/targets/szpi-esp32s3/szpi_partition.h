// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 这里只描述已有分区，不创建、移动或格式化任何分区。 */
typedef struct {
    uint32_t start_sector; /* 相对整张卡的起始扇区，1 扇区 = 512 字节。 */
    uint32_t sector_count; /* 区域长度：分区取 MBR，无分区卡取整卡容量；兼容 BPB 时不扩大。 */
    uint8_t index, type, fat_bits; /* 主分区编号、MBR 类型、探测得到的 FAT 位宽。 */
    char label[12]; /* 启动扇区卷标，仅作显示；不能用卷标判定文件系统。 */
} szpi_partition_t;

/* 从整卡 LBA 0 解析最多 4 个主分区；card_sectors 的单位为 512 字节扇区。
 * 返回写入 out 的项数；0 也可能表示表项非法，调用方不能继续按猜测写卡。 */
size_t szpi_mbr_scan(const uint8_t mbr[512], uint64_t card_sectors, szpi_partition_t out[4]);
/* boot 是该分区的首扇区；以 BPB 几何结构核验，成功后填写 fat_bits/label。 */
bool szpi_fat_probe(const uint8_t boot[512], szpi_partition_t *partition);
/* 返回数组下标而非 MBR 编号；-1 表示没有符合本工程策略的 FAT 分区。 */
int szpi_partition_choose(const szpi_partition_t *partitions, size_t count);
/* 将分区相对扇区映射为整卡 LBA；sector/count 均以 512 字节为单位。
 * 仅成功时输出 physical_sector；读写驱动必须先检查返回值再访问硬件。 */
bool szpi_partition_map(const szpi_partition_t *partition, uint32_t sector,
                        uint32_t count, uint32_t *physical_sector);
