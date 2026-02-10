#include "DS1302Manager.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char *TAG = "DS1302";

/* Registradores DS1302 */
#define DS1302_SECONDS   0x80
#define DS1302_MINUTES   0x82
#define DS1302_HOURS     0x84
#define DS1302_DATE      0x86
#define DS1302_MONTH     0x88
#define DS1302_DAY       0x8A
#define DS1302_YEAR      0x8C
#define DS1302_CONTROL   0x8E
#define DS1302_CHARGER   0x90
#define DS1302_CLOCK_BURST 0xBE

/* GPIOs */
static int PIN_RST;
static int PIN_CLK;
static int PIN_DAT;

/* ==== Utils ==== */

static inline void ds_delay(void) {
    esp_rom_delay_us(4);
}


static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

static uint8_t bcd_to_dec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

/* ==== Low level IO ==== */

static void dat_output(void) {
    gpio_set_direction(PIN_DAT, GPIO_MODE_OUTPUT);
}

static void dat_input(void) {
    gpio_set_direction(PIN_DAT, GPIO_MODE_INPUT);
}

static void write_byte(uint8_t value) {
    dat_output();
    for (int i = 0; i < 8; i++) {
        gpio_set_level(PIN_DAT, (value >> i) & 1);
        ds_delay();
        gpio_set_level(PIN_CLK, 1);
        ds_delay();
        gpio_set_level(PIN_CLK, 0);
        ds_delay();
    }
}

static uint8_t read_byte(void) {
    uint8_t value = 0;
    dat_input();
    for (int i = 0; i < 8; i++) {
        gpio_set_level(PIN_CLK, 1);
        ds_delay();
        if (gpio_get_level(PIN_DAT)) {
            value |= (1 << i);
        }
        gpio_set_level(PIN_CLK, 0);
        ds_delay();
    }
    return value;
}

static void write_reg(uint8_t reg, uint8_t value) {
    gpio_set_level(PIN_RST, 1);
    ds_delay();
    write_byte(reg);
    write_byte(value);
    gpio_set_level(PIN_RST, 0);
}

static uint8_t read_reg(uint8_t reg) {
    uint8_t val;
    gpio_set_level(PIN_RST, 1);
    ds_delay();
    write_byte(reg | 0x01);
    val = read_byte();
    gpio_set_level(PIN_RST, 0);
    return val;
}

/* ==== API pública ==== */

bool ds1302_begin(int rst_gpio, int clk_gpio, int dat_gpio) {
    PIN_RST = rst_gpio;
    PIN_CLK = clk_gpio;
    PIN_DAT = dat_gpio;

    ESP_LOGI(TAG, "Inicializando DS1302: RST=%d, CLK=%d, DAT=%d", 
             PIN_RST, PIN_CLK, PIN_DAT);

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_RST) | (1ULL << PIN_CLK) | (1ULL << PIN_DAT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&cfg);

    gpio_set_level(PIN_RST, 0);
    gpio_set_level(PIN_CLK, 0);
    gpio_set_level(PIN_DAT, 0);

    vTaskDelay(pdMS_TO_TICKS(100));

    /* Desabilita proteção de escrita */
    write_reg(DS1302_CONTROL, 0x00);
    ESP_LOGI(TAG, "Write protect desabilitado");

    /* IMPORTANTE: Verifica e desabilita Clock Halt (CH bit) */
    uint8_t sec_reg = read_reg(DS1302_SECONDS);
    if (sec_reg & 0x80) {
        ESP_LOGW(TAG, "DS1302 estava PARADO (CH bit ativo)! Iniciando relógio...");
        write_reg(DS1302_SECONDS, sec_reg & 0x7F);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Verifica se está funcionando */
    sec_reg = read_reg(DS1302_SECONDS);
    ESP_LOGI(TAG, "Registrador de segundos: 0x%02X", sec_reg);
    
    if (sec_reg == 0xFF || sec_reg == 0x00) {
        ESP_LOGE(TAG, "DS1302 pode não estar respondendo corretamente!");
        return false;
    }

    ESP_LOGI(TAG, "DS1302 inicializado com sucesso");
    return true;
}

bool ds1302_is_halted(void) {
    uint8_t sec = read_reg(DS1302_SECONDS);
    return (sec & 0x80) != 0;
}

void ds1302_set_halt(bool halt) {
    uint8_t sec = read_reg(DS1302_SECONDS);
    if (halt) {
        sec |= 0x80;
        ESP_LOGI(TAG, "Parando relógio (CH bit = 1)");
    } else {
        sec &= ~0x80;
        ESP_LOGI(TAG, "Iniciando relógio (CH bit = 0)");
    }
    write_reg(DS1302_SECONDS, sec);
}

RTCDateTime ds1302_get_datetime(void) {
    RTCDateTime dt;

    uint8_t sec   = read_reg(DS1302_SECONDS);
    uint8_t min   = read_reg(DS1302_MINUTES);
    uint8_t hour  = read_reg(DS1302_HOURS);
    uint8_t day   = read_reg(DS1302_DATE);
    uint8_t month = read_reg(DS1302_MONTH);
    uint8_t dow   = read_reg(DS1302_DAY);
    uint8_t year  = read_reg(DS1302_YEAR);

    dt.second = bcd_to_dec(sec & 0x7F);
    dt.minute = bcd_to_dec(min & 0x7F);
    dt.hour   = bcd_to_dec(hour & 0x3F);
    dt.day    = bcd_to_dec(day & 0x3F);
    dt.month  = bcd_to_dec(month & 0x1F);
    dt.year   = 2000 + bcd_to_dec(year);
    dt.dayOfWeek = bcd_to_dec(dow & 0x07);

    ESP_LOGD(TAG, "Lido do DS1302: %02d/%02d/%04d %02d:%02d:%02d", 
             dt.day, dt.month, dt.year, dt.hour, dt.minute, dt.second);

    return dt;
}

bool ds1302_set_datetime(const RTCDateTime *dt) {
    if (!dt) return false;

    /* Validação básica */
    if (dt->year < 2000 || dt->year > 2099 ||
        dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > 31 ||
        dt->hour > 23 ||
        dt->minute > 59 ||
        dt->second > 59) {
        ESP_LOGE(TAG, "Data/hora inválida: %02d/%02d/%04d %02d:%02d:%02d",
                 dt->day, dt->month, dt->year, dt->hour, dt->minute, dt->second);
        return false;
    }

    ESP_LOGI(TAG, "Configurando DS1302: %02d/%02d/%04d %02d:%02d:%02d",
             dt->day, dt->month, dt->year, dt->hour, dt->minute, dt->second);

    /* Desabilita proteção de escrita */
    write_reg(DS1302_CONTROL, 0x00);

    /* Para o relógio durante escrita para evitar inconsistências */
    uint8_t sec_bcd = dec_to_bcd(dt->second);
    write_reg(DS1302_SECONDS, sec_bcd | 0x80);  // CH = 1 (parado)

    /* Escreve todos os registradores */
    write_reg(DS1302_MINUTES, dec_to_bcd(dt->minute));
    write_reg(DS1302_HOURS,   dec_to_bcd(dt->hour) & 0x3F);  // Modo 24h
    write_reg(DS1302_DATE,    dec_to_bcd(dt->day));
    write_reg(DS1302_MONTH,   dec_to_bcd(dt->month));
    write_reg(DS1302_DAY,     dec_to_bcd(dt->dayOfWeek & 0x07));
    write_reg(DS1302_YEAR,    dec_to_bcd(dt->year % 100));

    /* Reinicia o relógio (limpa CH bit) */
    write_reg(DS1302_SECONDS, sec_bcd & 0x7F);  // CH = 0 (rodando)

    /* Verifica se foi escrito corretamente */
    vTaskDelay(pdMS_TO_TICKS(10));
    RTCDateTime verify = ds1302_get_datetime();
    
    if (verify.year == dt->year && verify.month == dt->month && verify.day == dt->day) {
        ESP_LOGI(TAG, "DS1302 configurado com sucesso!");
        return true;
    } else {
        ESP_LOGE(TAG, "Falha na verificação! Esperado: %04d-%02d-%02d, Lido: %04d-%02d-%02d",
                 dt->year, dt->month, dt->day, verify.year, verify.month, verify.day);
        return false;
    }
}

bool ds1302_is_valid(void) {
    RTCDateTime dt = ds1302_get_datetime();
    
    /* Verifica se a data parece válida (não é default/inválida) */
    if (dt.year < 2020 || dt.year > 2099) {
        ESP_LOGW(TAG, "Ano inválido: %d", dt.year);
        return false;
    }
    if (dt.month < 1 || dt.month > 12) {
        ESP_LOGW(TAG, "Mês inválido: %d", dt.month);
        return false;
    }
    if (dt.day < 1 || dt.day > 31) {
        ESP_LOGW(TAG, "Dia inválido: %d", dt.day);
        return false;
    }
    
    return true;
}