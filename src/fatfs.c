#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ff.h"
#include "tf_card.h"

#define LED_G 11

#define SPI_PORT spi0
#define MISO 16
#define CS 17
#define SCK 18
#define MOSI 19

FATFS fs;
FRESULT fr;
FIL file;
UINT bytes_written;

void setup_ledg() {
    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);
    gpio_put(LED_G, 0);
}

void setup_spi() {
    pico_fatfs_spi_config_t cfg = {
        SPI_PORT,
        100000,
        10000000,
        MISO,
        CS,
        SCK,
        MOSI,
        true
    };
    if (pico_fatfs_set_config(&cfg)) {
        printf("SPI configurado com sucesso!\n");
    } else {
        printf("Erro ao configurar o SPI!\n");
    }
}

bool microsd_mount() {
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printf("Erro ao montar micro SD: %d\n", fr);
        return false;
    } else {
        printf("Micro SD montado com sucesso!");
        return true;
    }
}

bool microsd_open() {
    fr = f_open(&file, "log.txt", FA_WRITE | FA_OPEN_APPEND);
    if (fr != FR_OK) {
        printf("Erro ao abrir arquivo log.txt: %d\n", fr);
        return false;
    } else {
        printf("Arquivo log.txt aberto com sucesso!\n");
        return true;
    }
}

void microsd_write(const char *payload) {
    fr = f_write(&file, payload, strlen(payload), &bytes_written);
    if (fr != FR_OK) {
        printf("Erro ao gravar no arquivo log.txt: %d\n", fr);
    } else {
        printf("Foram gravados %u bytes em log.txt!\n", bytes_written);
        gpio_put(LED_G, 1);
        sleep_ms(200);
        gpio_put(LED_G, 0);
    }
    f_sync(&file);
}




