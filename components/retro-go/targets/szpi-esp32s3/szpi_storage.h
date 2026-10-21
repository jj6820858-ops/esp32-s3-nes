// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "esp_err.h"
#include "driver/sdmmc_types.h"

/* 挂载已有分区；没有格式化、重新分区或擦除整卡的接口。 */
esp_err_t szpi_storage_mount(const char *base_path, sdmmc_card_t **out_card);
esp_err_t szpi_storage_unmount(const char *base_path);
