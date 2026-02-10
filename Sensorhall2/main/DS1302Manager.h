#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Estrutura equivalente ao RTCDateTime do Arduino */
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int dayOfWeek; // 0 = domingo
} RTCDateTime;

/* Inicialização */
bool ds1302_begin(int rst_gpio, int clk_gpio, int dat_gpio);

/* Controle de oscilador */
bool ds1302_is_halted(void);
void ds1302_set_halt(bool halt);

/* Leitura / Escrita */
RTCDateTime ds1302_get_datetime(void);
bool ds1302_set_datetime(const RTCDateTime *dt);

/* Validação - verifica se o horário armazenado é válido */
bool ds1302_is_valid(void);