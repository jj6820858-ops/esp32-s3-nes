#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "szpi_partition.h"

static void le16(uint8_t *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void le32(uint8_t *p, uint32_t v) { for (int i=0;i<4;i++) p[i]=v>>(i*8); }
static void entry(uint8_t *mbr, int n, uint32_t start, uint32_t count)
{
    uint8_t *p=mbr+446+n*16; p[4]=0x0c; le32(p+8,start); le32(p+12,count);
}
static void fat32(uint8_t *b, uint32_t total)
{
    memset(b,0,512); b[0]=0xeb; b[2]=0x90; b[510]=0x55;b[511]=0xaa;
    le16(b+11,512); b[13]=8; le16(b+14,32); b[16]=2;
    le32(b+32,total); le32(b+36,16384); le32(b+44,2);
    memcpy(b+71,"GAMES      ",11);
}
int main(void)
{
    uint8_t mbr[512]={0}, boot[512];
    mbr[510]=0x55; mbr[511]=0xaa;
    entry(mbr,0,225280,20480); entry(mbr,1,245760,1024000);
    entry(mbr,2,2097152,13631488);
    szpi_partition_t parts[4];
    assert(szpi_mbr_scan(mbr,15728640,parts)==3);
    assert(parts[2].start_sector==2097152);
    fat32(boot,13631488);
    assert(szpi_fat_probe(boot,&parts[2]));
    assert(parts[2].fat_bits==32 && strcmp(parts[2].label,"GAMES")==0);
    parts[0].fat_bits=16; parts[1].fat_bits=16;
    assert(szpi_partition_choose(parts,3)==2); /* 不能误选前面的 BIN/SDCARD。 */
    uint32_t physical;
    assert(szpi_partition_map(&parts[2],0,1,&physical) && physical==2097152);
    assert(szpi_partition_map(&parts[2],13631487,1,&physical) && physical==15728639);
    assert(!szpi_partition_map(&parts[2],13631487,2,&physical));
    assert(!szpi_partition_map(&parts[2],0,0,&physical));
    assert(!szpi_partition_map(&parts[2],UINT32_MAX,2,&physical));
    boot[13]=3; assert(!szpi_fat_probe(boot,&parts[2]));
    fat32(boot,13631490); assert(!szpi_fat_probe(boot,&parts[2]));
    mbr[510]=0; assert(szpi_mbr_scan(mbr,15728640,parts)==0);
    mbr[510]=0x55; entry(mbr,2,2097152,UINT32_MAX);
    assert(szpi_mbr_scan(mbr,15728640,parts)==0);
    entry(mbr,2,245760,13631488); /* 重叠分区表必须拒绝。 */
    assert(szpi_mbr_scan(mbr,15728640,parts)==0);

    /* 实卡回归：64GB 卡的 BPB 比 MBR 多写了 1 个扇区，但最后 37 个
     * 扇区不足一个簇，文件/FAT 数据仍全部位于原分区边界内。 */
    szpi_partition_t real_card={.index=3,.type=0x0c,
        .start_sector=2097152,.sector_count=122638335};
    fat32(boot,122638336);
    boot[13]=64; boot[16]=1; le32(boot+36,14971);
    assert(szpi_fat_probe(boot,&real_card) && real_card.fat_bits==32);
    assert(real_card.sector_count==122638335); /* 不能扩大实际读写区域。 */
    assert(szpi_partition_map(&real_card,122638334,1,&physical));
    assert(!szpi_partition_map(&real_card,122638334,2,&physical));
    assert(!szpi_partition_map(&real_card,122638335,1,&physical));

    le32(boot+32,122638337); /* 不把容错扩大成任意卷越界。 */
    assert(!szpi_fat_probe(boot,&real_card));

    fat32(boot,13631488);
    real_card.sector_count=13631487; /* 多出的扇区属于最后一个完整簇。 */
    assert(!szpi_fat_probe(boot,&real_card));

    /* FAT16 不使用这条 FAT32 尾部容错规则。 */
    memset(boot,0,sizeof(boot)); boot[510]=0x55; boot[511]=0xaa;
    le16(boot+11,512); boot[13]=4; le16(boot+14,4); boot[16]=2;
    le16(boot+17,512); le16(boot+19,20481); le16(boot+22,20);
    real_card.sector_count=20481;
    assert(szpi_fat_probe(boot,&real_card) && real_card.fat_bits==16);
    real_card.sector_count=20480;
    assert(!szpi_fat_probe(boot,&real_card));
    puts("SZPI partition: selection, BPB validation, overlap and write bounds passed");
    return 0;
}
