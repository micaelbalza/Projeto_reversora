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
        printf("Erro ao abrir log.txt\n");
        return false;
    }
    printf("Arquivo log.txt aberto com sucesso!\n");
    return true;
}

bool write_sd_card(FILE **f, const char* data) {
    // verifica se o arquivo existe e tenta escrever
    if (*f != NULL) {
        fprintf(*f, "%s\n", data);
        return true;
    }
    printf("Arquivo log.txt não aberto, impossível escrever!\n");
    return false;
}


void app_main(void) {

    esp_err_t ret;

    // configuração de montagem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // Evita formatar o cartão por erro de fiação
        .max_files = 5,
        .allocation_unit_size = 0
    };

    // configuração de barramento SPI
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        
    // velocidade reduzida para 200kHz (padrão de inicialização segura)
    host.max_freq_khz = 200; 

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t) PIN_NUM_CS;
    slot_config.host_id = host.slot;

    sdmmc_card_t *card;

    FILE *f;

    ret = setup_sd_card(host, bus_cfg);

    if (ret != ESP_OK) {
        while (true) {
            printf("Erro ao inicilizar o SPI!\n");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
    
    while (!mount_sd_card(ret, host, slot_config, mount_config, &card)) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    while (!open_sd_card(&f)) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    write_sd_card(&f, "Dado de teste gravado");
    fclose(f);
}