#include <stdio.h>
#include <string.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"  
#include "esp_err.h"
#include "stdbool.h"

// pinagem
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

esp_err_t setup_sd_card(sdmmc_host_t host, spi_bus_config_t bus_cfg) {
    // inicializa o barramento SPI e o driver SD
    printf("Inicializando barramento SPI...\n");
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        printf("Falha ao inicializar o SPI!\n");
        return ESP_FAIL;
    }
    printf("Barramento SPI inicializado com sucesso!\n");
    return ESP_OK;
}

bool mount_sd_card(esp_err_t ret, sdmmc_host_t host, sdspi_device_config_t slot_config, esp_vfs_fat_sdmmc_mount_config_t mount_config, sdmmc_card_t **card) {
    // monta o cartão SD
    printf("Tentando montar cartão SD...\n");
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, card);
    if (ret != ESP_OK) {
        printf("Erro 0x%x\n", ret);
        return false;
    }
    printf("Cartão SD montado com sucesso!\n");
    return true;
}

bool open_sd_card(FILE **f) {
    // grava o arquivo em formato de append, para que seja sempre adicionado no EOF
    *f = fopen("/sdcard/log.txt", "a");
    if (*f == NULL) {
        printf("Erro ao abrir arquivo!\n");
        return false;
    }
    printf("Arquivo arquivo aberto com sucesso!\n");
    return true;
}

bool write_sd_card(FILE **f, const char* data) {
    // verifica se o arquivo existe e tenta escrever
    if (*f != NULL) {
        fprintf(*f, "%s\n", data);
        fflush(*f);  // garante escrita imediata
        printf("Dados escritos com sucesso em arquivo!\n");
        return true;
    }
    printf("Arquivo não aberto, impossível escrever!\n");
    return false;
}

bool close_sd_card(FILE **f) {
    if (*f != NULL) {
        fclose(*f);
        printf("Arquivo arquivo fechado com sucesso!\n");
        return true;
    }
    printf("Arquivo arquivo não aberto, impossível fechar!\n");
    return false;
}