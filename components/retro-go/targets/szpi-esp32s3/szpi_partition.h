// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 这里只描述已有分区，不创建、移动或格式化任何分区。 */
typedef struct {
    uint32_t start_sector; /* 相对整张卡的起始扇区，1 扇区 = 512 字节。 */
    uint32_t sector_count;
    uint8_t index, type, fat_bits;
    char label[12];
} szpi_partition_t;

size_t szpi_mbr_scan(const uint8_t mbr[512], uint64_t card_sectors, szpi_partition_t out[4]);
bool szpi_fat_probe(const uint8_t boot[512], szpi_partition_t *partition);
int szpi_partition_choose(const szpi_partition_t *partitions, size_t count);
bool szpi_partition_map(const szpi_partition_t *partition, uint32_t sector,
                        uint32_t count, uint32_t *physical_sector);
