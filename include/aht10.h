// include/aht10.h
#pragma once
#include <stdbool.h>
#include "hardware/i2c.h"

// Endereço padrão do AHT10
#define AHT10_I2C_ADDR 0x38

// Inicializa o AHT10 em uma instância de I2C (configura pinos e calibra o sensor)
bool aht10_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin);

// Lê temperatura (°C) e umidade relativa (%) do AHT10.
// Retorna true em sucesso.
bool aht10_read(i2c_inst_t *i2c, float *temp_c, float *rh_pct);
