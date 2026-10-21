// SPDX-License-Identifier: GPL-2.0-or-later
#include "rg_system.h"
#include "szpi_partition.h"
#include "szpi_storage.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* 分层：文件路径 /sd/... → FatFs → 本分区扇区 → 整卡扇区 → SDMMC。 */
/* VFS 将 open/read/write 等文件接口交给 FatFs；FatFs 的 diskio 回调只见到
 * selected 这一片区域。回调加上 start_sector，才得到 SDMMC 的整卡 LBA。
 * 这使第三分区在 FatFs 看来从扇区 0 开始，挂载不依赖它自动选择 MBR 项。
 * 下列状态由一次挂载共用；unmount 同时负责初始化中途失败的回收。 */
static sdmmc_card_t *card;
static szpi_partition_t selected;
static BYTE drive=FF_DRV_NOT_USED;
static char drive_name[8];
static bool host_ready, vfs_ready;
static uint32_t fingerprint_lba[5], fingerprint_hash[5];
static unsigned fingerprint_count;

static DSTATUS partition_status(BYTE pdrv)
{
    return card && pdrv==drive ? 0 : STA_NOINIT;
}

static DRESULT partition_read(BYTE pdrv, BYTE *buffer, DWORD sector, UINT count)
{
    uint32_t physical;
    /* FatFs 传来的 sector/count 是分区内的 512 字节扇区，不是整卡地址。
     * 读取也必须过同一个映射器，防止错误参数读到其他分区的数据。 */
    if (partition_status(pdrv) || !buffer || !szpi_partition_map(&selected,sector,count,&physical))
        return RES_PARERR;
    return sdmmc_read_sectors(card,buffer,physical,count)==ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT partition_write(BYTE pdrv, const BYTE *buffer, DWORD sector, UINT count)
{
    uint32_t physical;
    /* 边界检查放在真正写卡之前，不能写到其他分区或 MBR。 */
    if (partition_status(pdrv) || !buffer || !szpi_partition_map(&selected,sector,count,&physical))
        return RES_PARERR;
    return sdmmc_write_sectors(card,buffer,physical,count)==ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT partition_ioctl(BYTE pdrv, BYTE command, void *buffer)
{
    if (partition_status(pdrv)) return RES_NOTRDY;
    if (command==CTRL_SYNC) return RES_OK; /* SDMMC 写入接口同步完成传输。 */
    if (!buffer) return RES_PARERR;
    switch (command) {
    case GET_SECTOR_COUNT: *(DWORD *)buffer=selected.sector_count; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buffer=512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buffer=1; return RES_OK;
    default: return RES_PARERR; /* 不提供 TRIM/整卡擦除命令。 */
    }
}

esp_err_t szpi_storage_unmount(const char *base_path)
{
    /* 先解除 FatFs 与 diskio，再注销 VFS，最后关闭硬件并释放卡描述符。
     * 标志只在相应步骤成功后设置，因此 fail 路径也能按已获资源清理。 */
    if (drive!=FF_DRV_NOT_USED) {
        f_mount(NULL,drive_name,0);
        ff_diskio_unregister(drive);
        drive=FF_DRV_NOT_USED;
    }
    if (vfs_ready) { esp_vfs_fat_unregister_path(base_path); vfs_ready=false; }
    if (host_ready) { sdmmc_host_deinit(); host_ready=false; }
    free(card); card=NULL;
    return ESP_OK;
}

static esp_err_t mount_once(const char *base_path, int speed)
{
    /* speed 单位为 kHz；扇区缓冲放内部 DMA 内存，不能用普通 PSRAM 替代。
     * 本函数只负责识别与挂载，任何失败都走同一回收出口，不自动格式化。 */
    esp_err_t err=ESP_FAIL;
    FATFS *fs=NULL;
    uint8_t *sector=heap_caps_malloc(512,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    card=calloc(1,sizeof(*card));
    if (!sector || !card) { err=ESP_ERR_NO_MEM; goto fail; }
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();
    host.flags=SDMMC_HOST_FLAG_1BIT; host.max_freq_khz=speed;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width=1; slot.clk=47; slot.cmd=48; slot.d0=21;
    slot.d1=slot.d2=slot.d3=GPIO_NUM_NC;
    slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    err=sdmmc_host_init(); if (err!=ESP_OK) goto fail;
    host_ready=true;
    err=sdmmc_host_init_slot(host.slot,&slot); if (err!=ESP_OK) goto fail;
    err=sdmmc_card_init(&host,card); if (err!=ESP_OK) goto fail;
    if (card->csd.sector_size!=512) { err=ESP_ERR_NOT_SUPPORTED; goto fail; }
    sdmmc_card_print_info(stdout,card);
    err=sdmmc_read_sectors(card,sector,0,1); if (err!=ESP_OK) goto fail;
    /* 首次读取时记录指纹；后面仅重读比较，不把缓存写回启动扇区。 */
    fingerprint_count=1;
    fingerprint_lba[0]=0; fingerprint_hash[0]=rg_hash((const char *)sector,512);

    /* 先识别无分区表的 FAT 卡；否则读取 MBR 中最多四个主分区。
     * 不以盘符或卷标选卡；每个候选分区还要验证自身 BPB 后才有 fat_bits。 */
    szpi_partition_t parts[4]={{.sector_count=card->csd.capacity}};
    size_t count;
    if (szpi_fat_probe(sector,&parts[0])) count=1;
    else {
        count=szpi_mbr_scan(sector,card->csd.capacity,parts);
        for (size_t i=0;i<count;i++) {
            if (sdmmc_read_sectors(card,sector,parts[i].start_sector,1)==ESP_OK) {
                szpi_fat_probe(sector,&parts[i]);
                fingerprint_lba[fingerprint_count]=parts[i].start_sector;
                fingerprint_hash[fingerprint_count++]=rg_hash((const char *)sector,512);
            }
            RG_LOGI("TF partition %u: start=%"PRIu32" sectors=%"PRIu32" FAT%u label='%s'",
                    parts[i].index,parts[i].start_sector,parts[i].sector_count,
                    parts[i].fat_bits,parts[i].label);
        }
    }
    int best=szpi_partition_choose(parts,count);
    if (best<0) { err=ESP_ERR_NOT_FOUND; goto fail; }
    selected=parts[best];
    RG_LOGI("TF selected partition %u: FAT%u, %"PRIu32" MiB, start=%"PRIu32,
            selected.index,selected.fat_bits,selected.sector_count/2048,selected.start_sector);
    /* 仅把选中的区域呈现给 FatFs，因此它看不到前面的两个旧系统分区。 */
    err=ff_diskio_get_drive(&drive); if (err!=ESP_OK) goto fail;
    static const ff_diskio_impl_t io={.init=partition_status,.status=partition_status,
        .read=partition_read,.write=partition_write,.ioctl=partition_ioctl};
    ff_diskio_register(drive,&io);
    snprintf(drive_name,sizeof(drive_name),"%u:",drive);
    /* VFS 注册只建立路径与逻辑驱动的联系，f_mount 才实际读取文件系统。 */
    err=esp_vfs_fat_register(base_path,drive_name,6,&fs); if (err!=ESP_OK) goto fail;
    vfs_ready=true;
    FRESULT result=f_mount(fs,drive_name,1);
    if (result!=FR_OK) {
        RG_LOGE("TF f_mount failed: %d; card kept unchanged",result);
        err=ESP_FAIL; goto fail;
    }
    free(sector);
    return ESP_OK;
fail:
    free(sector);
    szpi_storage_unmount(base_path);
    return err;
}

static void verify_filesystem(const char *base_path)
{
    /* 这是挂载后的文件级验收：建必要目录，再独占创建、写入、同步、重读
     * 一个短文件。通过只证明这次小范围操作成功，不代表全卡无坏块。
     * 探针结果只记录日志，不改变挂载返回值；部分前置失败会提前返回。
     * 不能把 storage_mount 的 ESP_OK 等同于探针通过。 */
    char path[128];
    const char *dirs[]={"/roms","/roms/nes","/retro-go"};
    for (size_t i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        snprintf(path,sizeof(path),"%s%s",base_path,dirs[i]);
        if (mkdir(path,0777)!=0 && errno!=EEXIST) {
            RG_LOGE("TF directory creation failed: %s errno=%d",path,errno); return;
        }
    }
    snprintf(path,sizeof(path),"%s/retro-go/szpi-check-%"PRIu32".tmp",base_path,
             (uint32_t)rg_system_timer());
    /* O_EXCL 保证不覆盖旧文件；验证后仅删除本次创建的临时文件。 */
    int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);
    if (fd<0) { RG_LOGE("TF write probe open failed: %d",errno); return; }
    const char expected[]="SZPI TF partition read/write check";
    char actual[sizeof(expected)]={0};
    bool good=write(fd,expected,sizeof(expected))==sizeof(expected);
    good=fsync(fd)==0 && good;
    close(fd);
    fd=open(path,O_RDONLY);
    if (fd>=0) { good=read(fd,actual,sizeof(actual))==sizeof(actual) && good; close(fd); }
    else good=false;
    good=good && !memcmp(actual,expected,sizeof(expected));
    unlink(path);
    RG_LOGI("TF file read/write probe: %s; /roms/nes ready",good ? "PASS" : "FAIL");

    /* 卡里还没有游戏时，也能用自编测试 ROM 验证显示、手柄与声音。 */
    extern const uint8_t szpi_demo_rom[];
    extern const size_t szpi_demo_rom_size;
    snprintf(path,sizeof(path),"%s/roms/nes/SZPI-Controls.nes",base_path);
    fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0600);
    if (fd>=0) {
        bool saved=write(fd,szpi_demo_rom,szpi_demo_rom_size)==szpi_demo_rom_size;
        saved=fsync(fd)==0 && saved; close(fd);
        if (!saved) unlink(path); /* 仅清理本次创建失败的文件。 */
        RG_LOGI("Self-authored NES demo: %s",saved ? "created on TF" : "write failed");
    } else if (errno==EEXIST) RG_LOGI("NES demo already on TF; kept unchanged");
    else RG_LOGE("NES demo create failed: errno=%d",errno);

    /* 对照写文件之前的指纹，验证整卡 MBR 和各分区启动扇区保持不变。
     * 这不是整卡所有文件的逐字节校验；完整写隔离由扇区边界检查负责。 */
    /* 指纹覆盖的是成功读到的启动扇区，且 hash 相等不是逐字节证明；
     * 它提供启动元数据未发生可检测变化的证据，不能替代边界约束。 */
    uint8_t *sector=heap_caps_malloc(512,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    if (sector) {
        bool preserved=true;
        for (unsigned i=0;i<fingerprint_count;i++)
            if (sdmmc_read_sectors(card,sector,fingerprint_lba[i],1)!=ESP_OK ||
                rg_hash((const char *)sector,512)!=fingerprint_hash[i]) preserved=false;
        RG_LOGI("TF MBR/partition boot sectors unchanged: %s (%u sectors)",
                preserved ? "PASS" : "FAIL",fingerprint_count);
        free(sector);
    }
}

esp_err_t szpi_storage_mount(const char *base_path, sdmmc_card_t **out_card)
{
    if (!base_path || !out_card || card) return ESP_ERR_INVALID_ARG;
    esp_err_t err=mount_once(base_path,SDMMC_FREQ_DEFAULT);
    /* 仅通信层超时、响应或 CRC 错误才降频重试；BPB/分区布局不合法与
     * 时钟速度无关，不能通过反复重试或扩大分区来掩盖。 */
    if (err==ESP_ERR_TIMEOUT || err==ESP_ERR_INVALID_RESPONSE || err==ESP_ERR_INVALID_CRC) {
        RG_LOGW("TF retry at probing speed; no card data changed");
        err=mount_once(base_path,SDMMC_FREQ_PROBING);
    }
    if (err==ESP_OK) { *out_card=card; verify_filesystem(base_path); }
    return err;
}
