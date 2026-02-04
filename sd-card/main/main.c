#include <stdio.h>
#include <string.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"     

// Definição dos pinos
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   4

void app_main(void) {
    esp_err_t ret;

    // 1. Configuração de montagem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // Evita formatar o cartão por erro de fiação
        .max_files = 5,
        .allocation_unit_size = 0
    };

    // 2. Inicializar barramento SPI
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    
    // Velocidade reduzida para 200kHz (padrão de inicialização segura)
    host.max_freq_khz = 200; 

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    printf("Inicializando barramento SPI (200kHz)...\n");
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        while(1) {
            printf("Falha ao inicializar o SPI!\n");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }

    // 3. Configurar slot SD
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // 4. Montar o cartão
    while(1) {
        sdmmc_card_t *card;
        printf("Tentando montar cartao SD...\n");
        ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            printf("Erro 0x%x\n", ret);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        } else {
            break;
        }
    }

    // 5. Gravar no arquivo (Modo Append "a")
    printf("Cartão montado! Abrindo arquivo...\n");
    FILE *f = fopen("/sdcard/log.txt", "a");
    if (f == NULL) {
        while(1) {
            printf("Erro ao abrir log.txt\n");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    } else {
        fprintf(f, "Dado de teste gravado\n");
        fclose(f);
        while(1) {
            printf("Dado gravado com sucesso\n");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
    }
}