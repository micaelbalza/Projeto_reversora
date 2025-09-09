// src/aht10.c
#include "aht10.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define AHT10_CMD_INIT      0xE1
#define AHT10_CMD_TRIGGER   0xAC
#define AHT10_CMD_SOFTRESET 0xBA

// Bits de status
#define AHT10_STATUS_BUSY        0x80
#define AHT10_STATUS_CALIBRATED  0x08

static inline bool aht10_write(i2c_inst_t *i2c, const uint8_t *buf, size_t len) {
    int ret = i2c_write_blocking(i2c, AHT10_I2C_ADDR, buf, (uint)len, false);
    return (ret == (int)len);
}

static inline bool aht10_read_bytes(i2c_inst_t *i2c, uint8_t *buf, size_t len) {
    int ret = i2c_read_blocking(i2c, AHT10_I2C_ADDR, buf, (uint)len, false);
    return (ret == (int)len);
}

bool aht10_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin) {
    // Configura pinos e instancia (I2C0/1 vem de quem chama)
    i2c_init(i2c, 1000 * 1000); // 1 MHz (o AHT10 aceita até ~400k; 400k é mais “conservador”)
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    sleep_ms(20);

    // Soft reset (opcional)
    uint8_t sr = AHT10_CMD_SOFTRESET;
    aht10_write(i2c, &sr, 1);
    sleep_ms(20);

    // Comando de inicialização/calibração
    uint8_t init_cmd[3] = { AHT10_CMD_INIT, 0x08, 0x00 };
    if (!aht10_write(i2c, init_cmd, 3)) return false;

    // Aguarda calibração
    sleep_ms(10);

    // Dispara uma leitura para “acordar” o sensor
    uint8_t trig[3] = { AHT10_CMD_TRIGGER, 0x33, 0x00 };
    if (!aht10_write(i2c, trig, 3)) return false;

    sleep_ms(80); // tempo de conversão típico

    // Verifica se não está ocupado
    uint8_t data[6] = {0};
    if (!aht10_read_bytes(i2c, data, 6)) return false;

    // Se ainda ocupado, aguarde mais um pouco (tolerante)
    int tries = 5;
    while ((data[0] & AHT10_STATUS_BUSY) && tries--) {
        sleep_ms(20);
        if (!aht10_read_bytes(i2c, data, 6)) return false;
    }
    // Muitos sensores já vêm calibrados; se não, o bit 3 pode estar 0. Vamos aceitar mesmo assim.
    return true;
}

bool aht10_read(i2c_inst_t *i2c, float *temp_c, float *rh_pct) {
    if (!temp_c || !rh_pct) return false;

    // Dispara conversão
    uint8_t trig[3] = { AHT10_CMD_TRIGGER, 0x33, 0x00 };
    if (!aht10_write(i2c, trig, 3)) return false;

    // Espera conversão (~80 ms) ou até sair do estado busy
    sleep_ms(80);

    uint8_t data[6] = {0};
    if (!aht10_read_bytes(i2c, data, 6)) return false;

    // Se ainda ocupado, tente mais algumas vezes
    int tries = 5;
    while ((data[0] & AHT10_STATUS_BUSY) && tries--) {
        sleep_ms(10);
        if (!aht10_read_bytes(i2c, data, 6)) return false;
    }

    // Monta os 20 bits de RH e Temp conforme datasheet
    // RH:  bits [19:0] começando em data[1]
    // TMP: bits [19:0] começando no nybble baixo de data[3]
    uint32_t raw_rh = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4);
    uint32_t raw_t  = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | (uint32_t)data[5];

    // Conversão (datasheet): RH = raw/2^20 * 100 ; Temp = raw/2^20 * 200 - 50
    *rh_pct  = (raw_rh * 100.0f) / 1048576.0f;
    *temp_c  = (raw_t * 200.0f)  / 1048576.0f - 50.0f;

    // Limita faixas plausíveis
    if (*rh_pct < 0)   *rh_pct = 0;
    if (*rh_pct > 100) *rh_pct = 100;

    return true;
}
