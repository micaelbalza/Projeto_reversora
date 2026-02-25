#pragma once

#include <time.h>
#include "esp_err.h"

#ifndef I2C_SDA_PIN      // <- mesmo nome usado no .c
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

esp_err_t i2c_master_init(void);
esp_err_t ds3231_get_time(struct tm *t);
esp_err_t ds3231_set_time(const struct tm *t);
void ntp_sync_and_save_to_ds3231(void);
