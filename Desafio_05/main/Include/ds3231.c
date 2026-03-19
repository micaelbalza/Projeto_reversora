#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_sntp.h"        /* NTP — incluso no ESP-IDF (lwip/sntp) */
#include "driver/i2c.h"

#include "esp_log.h"          // ESP_LOGI, ESP_LOGE, ESP_LOGW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"    // vTaskDelay, pdMS_TO_TICKS
#include "ds3231.h"           // o próprio header!

static const char *TAG = "ds3231";

/* NTP */
#define NTP_SERVER         "pool.ntp.org"       /* Servidor NTP */
#define NTP_TIMEOUT_MS     10000               /* Tempo máximo aguardando sync */
/* Fuso horário POSIX:
 *   UTC puro      → ""
 *   Brasília      → "BRT3"          (UTC-3, sem horário de verão)
 *   São Paulo DST → "BRT3BRST,M10.3.0,M2.3.0" (com horário de verão) */
#define TIMEZONE           "BRT3"

/* Intervalo de sincronização */
#define SYNC_INTERVAL_S    10   /* A cada quantos segundos tenta sync NTP */

/* I2C / DS3231 */
#define I2C_PORT           I2C_NUM_0
// #define I2C_SDA_PIN        21       /* Pino SDA — ajuste para seu hardware */
// #define I2C_SCL_PIN        22       /* Pino SCL — ajuste para seu hardware */
#define I2C_FREQ_HZ        100000  /* 100 kHz é seguro para o DS3231 */
#define DS3231_ADDR        0x68    /* Endereço I2C fixo do chip DS3231 */


// auxiliares para conversão de padrao bcd para decimal e viceversa
static inline uint8_t bcd_to_dec(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static inline uint8_t dec_to_bcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}



/* ===========================================================================
 * I2C — INICIALIZAÇÃO
 * =========================================================================*/

esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_PIN,
        .scl_io_num       = I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,   /* Resistores internos do ESP32 */
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,   /* Módulo DS3231 já tem pull-ups externos */
        .master.clk_speed = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_PORT, conf.mode,
                              0 /* rx buf — não usado em master */,
                              0 /* tx buf — não usado em master */,
                              0 /* flags */);
}


/* ===========================================================================
 * DS3231 — LEITURA
 *
 * Registradores do DS3231 (começando em 0x00):
 *   0x00 → Segundos  (BCD, bit7 = Oscillator Stop Flag — ignorar na leitura)
 *   0x01 → Minutos   (BCD)
 *   0x02 → Horas     (BCD, bit6=0 → modo 24h)
 *   0x03 → Dia da semana (1–7, não usamos aqui)
 *   0x04 → Dia do mês (BCD)
 *   0x05 → Mês       (BCD, bit7 = century flag)
 *   0x06 → Ano       (BCD, 00–99 representando 2000–2099)
 * =========================================================================*/

esp_err_t ds3231_get_time(struct tm *t)
{
    uint8_t buf[7];
    uint8_t reg_start = 0x00;

    /* Passo 1: escreve o endereço do registrador inicial */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_start, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 write reg address failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Passo 2: lê 7 bytes consecutivos */
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, 6, I2C_MASTER_ACK);   /* 6 bytes com ACK */
    i2c_master_read_byte(cmd, &buf[6], I2C_MASTER_NACK); /* último byte com NACK */
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 read failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Converte BCD → decimal e preenche struct tm (padrão C) */
    t->tm_sec  = bcd_to_dec(buf[0] & 0x7F);      /* máscara remove bit OSF */
    t->tm_min  = bcd_to_dec(buf[1] & 0x7F);
    t->tm_hour = bcd_to_dec(buf[2] & 0x3F);      /* máscara garante modo 24h */
    /* buf[3] = dia da semana — ignoramos, mktime() recalcula */
    t->tm_mday = bcd_to_dec(buf[4] & 0x3F);
    t->tm_mon  = bcd_to_dec(buf[5] & 0x1F) - 1;  /* struct tm: mês 0–11 */
    t->tm_year = bcd_to_dec(buf[6]) + 100;        /* struct tm: anos desde 1900
                                                      DS3231 00 = 2000, logo +100 */
    t->tm_isdst = -1; /* deixa o sistema calcular horário de verão */
    return ESP_OK;
}


/* ===========================================================================
 * DS3231 — ESCRITA
 * =========================================================================*/

esp_err_t ds3231_set_time(const struct tm *t)
{
    /* data[0] = endereço do registrador; data[1..7] = valores */
    uint8_t data[8];
    data[0] = 0x00;                              /* começa no reg 0x00 */
    data[1] = dec_to_bcd(t->tm_sec);
    data[2] = dec_to_bcd(t->tm_min);
    data[3] = dec_to_bcd(t->tm_hour);           /* sempre modo 24h */
    data[4] = dec_to_bcd(t->tm_wday + 1);       /* DS3231 usa 1–7 */
    data[5] = dec_to_bcd(t->tm_mday);
    data[6] = dec_to_bcd(t->tm_mon + 1);        /* DS3231 usa 1–12 */
    data[7] = dec_to_bcd(t->tm_year - 100);     /* 2-dígitos: 2024 → 24 */

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, sizeof(data), true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 write time failed: %s", esp_err_to_name(err));
    }
    return err;
}


/* ===========================================================================
 * NTP — SINCRONIZAR E GRAVAR NO DS3231
 * =========================================================================*/

void ntp_sync_and_save_to_ds3231(void)
{
    /* Configura e inicia o cliente SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_init();

    ESP_LOGI(TAG, "Aguardando sincronização NTP com %s ...", NTP_SERVER);

    /* Espera até o status mudar de RESET ou esgotar o timeout */
    int elapsed = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET
           && elapsed < NTP_TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        elapsed += 500;
    }

    /* Verifica se o tempo obtido faz sentido (ano > 2020) */
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year > (2020 - 1900)) {
        /* Aplica fuso horário configurado antes de gravar */
        setenv("TZ", TIMEZONE, 1);
        tzset();
        time(&now);
        localtime_r(&now, &timeinfo);

        ESP_LOGI(TAG, "NTP OK → %04d-%02d-%02d %02d:%02d:%02d (TZ: %s)",
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon  + 1,
                 timeinfo.tm_mday,
                 timeinfo.tm_hour,
                 timeinfo.tm_min,
                 timeinfo.tm_sec,
                 TIMEZONE);

        if (ds3231_set_time(&timeinfo) == ESP_OK) {
            ESP_LOGI(TAG, "Hora gravada no DS3231 com sucesso.");
        }
    } else {
        ESP_LOGW(TAG, "Sync NTP falhou (ano inválido). DS3231 mantém hora anterior.");
    }

    esp_sntp_stop(); /* Para o cliente SNTP — não precisamos dele rodando o tempo todo */
}
