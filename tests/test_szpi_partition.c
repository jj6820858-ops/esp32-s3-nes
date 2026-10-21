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
    fat32(boot,13631489); assert(!szpi_fat_probe(boot,&parts[2]));
    mbr[510]=0; assert(szpi_mbr_scan(mbr,15728640,parts)==0);
    mbr[510]=0x55; entry(mbr,2,2097152,UINT32_MAX);
    assert(szpi_mbr_scan(mbr,15728640,parts)==0);
    entry(mbr,2,245760,13631488); /* 重叠分区表必须拒绝。 */
    assert(szpi_mbr_scan(mbr,15728640,parts)==0);
    puts("SZPI partition: selection, BPB validation, overlap and write bounds passed");
    return 0;
}
