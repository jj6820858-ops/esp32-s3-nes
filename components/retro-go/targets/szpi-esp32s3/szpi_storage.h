// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "esp_err.h"
#include "driver/sdmmc_types.h"

/* 挂载已有分区；没有格式化、重新分区或擦除整卡的接口。 */
/* base_path 为 VFS 路径前缀（如 /sd），out_card 成功时返回本模块管理的卡。
 * 挂载成功后另做文件读写探针，探针结果见日志；它不是整卡健康检测。 */
esp_err_t szpi_storage_mount(const char *base_path, sdmmc_card_t **out_card);
/* 释放挂载所获资源；base_path 应与挂载时相同，释放后不可再使用 out_card。 */
esp_err_t szpi_storage_unmount(const char *base_path);
