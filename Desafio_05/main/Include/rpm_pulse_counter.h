#pragma once
/**
 * @file rpm_pulse_counter.h
 * @brief Módulo de contagem de pulsos e estimação de RPM via interrupção (ISR) no ESP32.
 *
 * ============================================================
 * COMO USAR (VISÃO GERAL)
 * ============================================================
 *
 * 1) IMPORTANTE: NVS DEVE SER INICIALIZADO NO INÍCIO DO app_main()
 *    ------------------------------------------------------------
 *    Este módulo SALVA e CARREGA o valor PPR (pulsos por revolução) no NVS.
 *    Portanto, o NVS precisa estar pronto ANTES de chamar rpm_counter_init().
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
 * 2) INIT DO MÓDULO
 *    ------------------------------------------------------------
 *    RpmCounterConfig cfg = {
 *        .encoder_gpio = GPIO_NUM_32,
 *        .num_calib_points = 3,
 *        .calib_target_rpm = {1000.0f, 1200.0f, 1500.0f},
 *        .calib_window_seconds = 3.0f,
 *    };
 *    ESP_ERROR_CHECK(rpm_counter_init(&cfg));
 *
 * 3) PPR (pulsos por revolução)
 *    ------------------------------------------------------------
 *    O PPR é necessário para converter pulsos/s -> RPM:
 *      RPM = (pulsos_por_segundo * 60) / PPR
 *
 *    O PPR pode vir de:
 *      (a) NVS (carregado automaticamente no init),
 *      (b) manual (rpm_counter_set_estimated_ppr(ppr, persist)),
 *      (c) calibração completa (rpm_counter_run_full_calibration()).
 *
 * 4) CALIBRAÇÃO (CONTROLADA PELA MAIN)
 *    ------------------------------------------------------------
 *    Este módulo NÃO gerencia botões.
 *    Sua main pode definir, por exemplo:
 *      - botão pressionado no boot => rpm_counter_clear_ppr_calibration()
 *      - clique curto => “OK, capturar agora” (na sua lógica)
 *
 *    A calibração completa (run_full_calibration) é BLOQUEANTE e mede
 *    pulsos/s por alguns segundos em cada ponto de RPM alvo.
 *
 * 5) LEITURA DE RPM
 *    ------------------------------------------------------------
 *    float rpm;
 *    if (rpm_counter_measure_rpm_blocking(1.0f, &rpm) == ESP_OK) {
 *        // usar rpm
 *    }
 *
 *    Se PPR inválido (não calibrado), retorna ESP_FAIL.
 *
 * 6) ENTRADA DE SINAL
 *    ------------------------------------------------------------
 *    - O sensor deve fornecer um sinal digital limpo (0..3.3V).
 *    - O módulo usa ISR em borda de subida e um filtro simples de "glitch"
 *      por tempo mínimo entre pulsos.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum number of calibration RPM points supported.
#define RPM_COUNTER_MAX_CALIB_POINTS  8

typedef struct
{
    gpio_num_t encoder_gpio; /**< GPIO receiving encoder/sensor pulses. */

    int   num_calib_points;  /**< Number of target RPM points for calibration. */
    float calib_target_rpm[RPM_COUNTER_MAX_CALIB_POINTS]; /**< Target RPM list. */
    float calib_window_seconds; /**< Measurement time window for each calibration point (seconds). */
} RpmCounterConfig;

/**
 * @brief Initialize the RPM counter module.
 *
 * This function:
 *  - Copies configuration
 *  - Configures the encoder GPIO as input with internal pull-down
 *  - Installs ISR to count rising edges
 *  - Tries to load previously saved PPR from NVS (if available)
 *
 * @note Requires NVS initialized in app_main() before calling this.
 *
 * @param config Pointer to configuration structure.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rpm_counter_init(const RpmCounterConfig *config);

/**
 * @brief Run full calibration to discover the encoder PPR and persist it in NVS.
 *
 * For each target RPM:
 *  - User must set motor/shaft speed to that RPM
 *  - Module measures pulses/s over calib_window_seconds
 *  - Computes PPR_i = (pulses_per_second * 60) / target_rpm
 *
 * Final PPR = average(PPR_i) of valid points. Then it is saved to NVS.
 *
 * @note Blocking function.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rpm_counter_run_full_calibration(void);

/**
 * @brief Clear stored PPR calibration from RAM and NVS.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rpm_counter_clear_ppr_calibration(void);

/**
 * @brief Convenience API: clear calibration + run full calibration.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rpm_counter_recalibrate(void);

/**
 * @brief Manually set the pulses-per-revolution (PPR).
 *
 * @param ppr Pulses per revolution (> 0).
 * @param persist If true, save to NVS.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if ppr <= 0.
 */
esp_err_t rpm_counter_set_estimated_ppr(float ppr, bool persist);

/**
 * @brief Get the current estimated PPR.
 *
 * @return PPR (>0 if valid, <=0 if not calibrated).
 */
float rpm_counter_get_estimated_ppr(void);

/**
 * @brief Blocking RPM measurement using current PPR.
 *
 * @param measurement_window_seconds Time window in seconds.
 * @param[out] out_rpm Output RPM.
 * @return ESP_OK on success, ESP_FAIL if PPR invalid, or error code otherwise.
 */
esp_err_t rpm_counter_measure_rpm_blocking(float measurement_window_seconds,
                                          float *out_rpm);

/**
 * @brief Debug helper: measure raw pulses, pulses/s and RPM.
 *
 * @param measurement_window_seconds Time window in seconds.
 * @param[out] out_pulses Raw pulses counted during window.
 * @param[out] out_pulses_per_second Pulses per second.
 * @param[out] out_rpm Computed RPM.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rpm_counter_measure_debug(float measurement_window_seconds,
                                   uint32_t *out_pulses,
                                   float *out_pulses_per_second,
                                   float *out_rpm);

#ifdef __cplusplus
}
#endif
