#ifndef MICRO_SD_H
#define MICRO_SD_H

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

esp_err_t setup_sd_card(sdmmc_host_t host, spi_bus_config_t bus_cfg);

bool mount_sd_card(
    esp_err_t ret,
    sdmmc_host_t host,
    sdspi_device_config_t slot_config,
    esp_vfs_fat_sdmmc_mount_config_t mount_config,
    sdmmc_card_t **card
);

bool open_sd_card(FILE **f);

bool write_sd_card(FILE **f, const char* data);

bool close_sd_card(FILE **f);

#endif
