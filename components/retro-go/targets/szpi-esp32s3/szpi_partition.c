// SPDX-License-Identifier: GPL-2.0-or-later
#include "szpi_partition.h"
#include <string.h>

/* MBR 和 FAT 的多字节字段采用小端；逐字节读，避免未对齐访问。 */
static uint32_t read16(const uint8_t *p) { return p[0] | ((uint32_t)p[1]<<8); }
static uint32_t read32(const uint8_t *p) { return read16(p) | (read16(p+2)<<16); }

size_t szpi_mbr_scan(const uint8_t mbr[512], uint64_t card_sectors, szpi_partition_t out[4])
{
    if (!mbr || !out || mbr[510]!=0x55 || mbr[511]!=0xaa) return 0;
    size_t count=0;
    for (int i=0;i<4;i++) {
        const uint8_t *p=mbr+446+i*16;
        uint32_t start=read32(p+8), sectors=read32(p+12);
        if (!p[4] && !sectors) continue;
        /* 拒绝 GPT、越界及不完整表项，错误时保持整卡不变。 */
        if (p[4]==0xee || !start || !sectors || (uint64_t)start+sectors>card_sectors)
            return 0;
        for (size_t n=0;n<count;n++)
            if ((uint64_t)start < (uint64_t)out[n].start_sector+out[n].sector_count &&
                (uint64_t)out[n].start_sector < (uint64_t)start+sectors) return 0;
        out[count++]=(szpi_partition_t){.index=i+1,.type=p[4],
            .start_sector=start,.sector_count=sectors};
    }
    return count;
}

bool szpi_fat_probe(const uint8_t b[512], szpi_partition_t *p)
{
    if (!b || !p) return false;
    p->fat_bits=0; p->label[0]=0;
    if (b[510]!=0x55 || b[511]!=0xaa || read16(b+11)!=512) return false;
    uint32_t cluster=b[13], reserved=read16(b+14), fats=b[16];
    if (!cluster || cluster>128 || (cluster&(cluster-1)) || !reserved || !fats || fats>2)
        return false;
    uint32_t total=read16(b+19); if (!total) total=read32(b+32);
    uint32_t fat=read16(b+22); if (!fat) fat=read32(b+36);
    uint32_t roots=read16(b+17), root_sectors=(roots*32+511)/512;
    uint64_t overhead=(uint64_t)reserved+fats*(uint64_t)fat+root_sectors;
    if (!fat || !total || overhead>=total) return false;
    uint32_t clusters=(total-overhead)/cluster;
    unsigned bits=clusters<4085 ? 12 : clusters<65525 ? 16 : 32;
    /* 部分已有 FAT32 卡的 BPB 总长度比 MBR 多记 1 个扇区。
     * 仅当该扇区属于不足一个簇的无用尾部时接受：完整数据簇、FAT 表
     * 仍须全部位于分区内。绝不扩大 sector_count，底层读写边界不变。 */
    if (total>p->sector_count &&
        (bits!=32 || total-p->sector_count!=1 ||
         overhead+(uint64_t)clusters*cluster>p->sector_count)) return false;
    /* 不只相信分区类型或标签：核对真正的 FAT 几何结构和容量。 */
    if ((bits==32 && (roots || read16(b+22) || read16(b+42) || read32(b+44)<2 ||
                     read32(b+44)>=(uint64_t)clusters+2)) ||
        (bits!=32 && (!roots || !read16(b+22)))) return false;
    if (bits==12 || (uint64_t)(clusters+2)*(bits/8)>(uint64_t)fat*512) return false;
    p->fat_bits=bits;
    memcpy(p->label,b+(bits==32 ? 71 : 43),11); p->label[11]=0;
    for (int i=10;i>=0 && p->label[i]==' ';i--) p->label[i]=0;
    return true;
}

int szpi_partition_choose(const szpi_partition_t *partitions, size_t count)
{
    int best=-1;
    for (size_t i=0;i<count;i++) {
        const szpi_partition_t *p=partitions+i;
        /* 多分区卡仅选最大的 FAT32，避免把游戏写入旧系统的小分区。 */
        if (p->fat_bits!=32 && !(count==1 && p->fat_bits==16)) continue;
        if (best<0 || p->sector_count>partitions[best].sector_count) best=(int)i;
    }
    return best;
}

bool szpi_partition_map(const szpi_partition_t *p, uint32_t sector,
                        uint32_t count, uint32_t *physical_sector)
{
    /* 每一次底层读写都检查边界，不能落到 BIN、SDCARD 或整卡 MBR。 */
    if (!p || !physical_sector || !count || sector>=p->sector_count ||
        count>p->sector_count-sector || (uint64_t)p->start_sector+sector+count>UINT32_MAX)
        return false;
    *physical_sector=p->start_sector+sector;
    return true;
}
