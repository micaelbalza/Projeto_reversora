#pragma once
/**
 * @file joystick_hall.h
 * @brief Módulo de leitura e calibração de manche (Hall) via ADC1 no ESP32.
 *
 * ============================================================
 * COMO USAR (VISÃO GERAL)
 * ============================================================
 *
 * 1) IMPORTANTE: NVS DEVE SER INICIALIZADO NO INÍCIO DO app_main()
 *    ------------------------------------------------------------
 *    Este módulo SALVA e CARREGA calibração no NVS.
 *    Portanto, o NVS precisa estar pronto ANTES de chamar joystick_hall_init().
 *
 *    Exemplo (no app_main):
 *      esp_err_t err = nvs_flash_init();
 *      if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
 *          ESP_ERROR_CHECK(nvs_flash_erase());
 *          ESP_ERROR_CHECK(nvs_flash_init());
 *      }
 *
 *    OBS: Este módulo NÃO chama nvs_flash_init() internamente.
 *
 * 2) CONFIGURAÇÃO DO ADC + INIT
 *    ------------------------------------------------------------
 *    joystick_hall_config_t cfg = {
 *        .sensor_a_ch = ADC1_CHANNEL_5, // GPIO33
 *        .sensor_b_ch = ADC1_CHANNEL_6, // GPIO34
 *        .width = ADC_WIDTH_BIT_12,
 *        .atten = ADC_ATTEN_DB_12,
 *        .samples_per_read = 32,
 *        .deadband_percent = 5,
 *    };
 *
 *    ESP_ERROR_CHECK(joystick_hall_init(&cfg));
 *
 * 3) FLUXO DE CALIBRAÇÃO (CONTROLADO PELA MAIN)
 *    ------------------------------------------------------------
 *    O módulo não gerencia botões.
 *    A lógica típica na main é:
 *
 *    - Se botão pressionado no BOOT: joystick_hall_clear_calibration()
 *    - Se não existe calibração: pedir ao usuário para posicionar:
 *        a) MEIO  -> joystick_hall_capture_point(JH_POS_MID)
 *        b) FRENTE-> joystick_hall_capture_point(JH_POS_FRONT)
 *        c) TRÁS  -> joystick_hall_capture_point(JH_POS_BACK)
 *      Depois: joystick_hall_save_calibration()
 *
 * 4) LEITURA NORMAL
 *    ------------------------------------------------------------
 *    joystick_hall_reading_t r;
 *    ESP_ERROR_CHECK(joystick_hall_read(&r));
 *
 *    r.front_percent: 0..100
 *    r.back_percent : 0..100
 *    r.state        : MID / FRONT / BACK / NOT_CALIBRATED
 *
 * 5) DEBUG RAW
 *    ------------------------------------------------------------
 *    int a,b,d;
 *    ESP_ERROR_CHECK(joystick_hall_read_raw(&a,&b,&d));
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JH_POS_MID = 0,
    JH_POS_FRONT = 1,
    JH_POS_BACK = 2,
} jh_cal_point_t;

typedef enum {
    JH_STATE_NOT_CALIBRATED = 0,
    JH_STATE_MID,
    JH_STATE_FRONT,
    JH_STATE_BACK,
} jh_state_t;

typedef struct {
    // ADC1 channels (ESP32 legacy ADC)
    adc1_channel_t sensor_a_ch;   // ex: ADC1_CHANNEL_5 (GPIO33)
    adc1_channel_t sensor_b_ch;   // ex: ADC1_CHANNEL_6 (GPIO34)

    adc_bits_width_t width;       // ADC_WIDTH_BIT_12
    adc_atten_t atten;            // ADC_ATTEN_DB_12

    int samples_per_read;         // média por leitura (ex: 32)
    int deadband_percent;         // zona morta em torno do meio (ex: 5)
} joystick_hall_config_t;

typedef struct {
    // Leitura bruta
    int a_raw;
    int b_raw;
    int d_raw;          // a_raw - b_raw

    // Percentuais finais
    int front_percent;  // 0..100
    int back_percent;   // 0..100

    jh_state_t state;
} joystick_hall_reading_t;

// ------------------ API ------------------

/**
 * @brief Inicializa ADC1, carrega calibração do NVS (se existir) e prepara o módulo.
 *
 * @note Requer NVS inicializado antes do init (ver cabeçalho).
 */
esp_err_t joystick_hall_init(const joystick_hall_config_t *cfg);

// Retorna true se calibração válida foi carregada do NVS
bool joystick_hall_is_calibrated(void);

// Limpa calibração do NVS e RAM
esp_err_t joystick_hall_clear_calibration(void);

// Captura UM ponto de calibração (MEIO/FRENTE/TRÁS) e guarda em RAM
// (A main controla o botão e a sequência)
esp_err_t joystick_hall_capture_point(jh_cal_point_t point);

// Depois de capturar os 3 pontos, salva definitivamente no NVS
esp_err_t joystick_hall_save_calibration(void);

// Lê e calcula posição (% frente / % trás)
esp_err_t joystick_hall_read(joystick_hall_reading_t *out);

// Debug: pega os valores brutos (sem inferência)
esp_err_t joystick_hall_read_raw(int *out_a, int *out_b, int *out_d);

#ifdef __cplusplus
}
#endif
