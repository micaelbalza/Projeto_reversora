#pragma once

/**
 * @file rpm_pulse_counter.h
 * @brief Pulse counting and RPM estimation module for ESP32.
 *
 * Features:
 *  - Counts pulses on a GPIO pin using an ISR (rising edge).
 *  - Glitch filter (minimum interval between pulses).
 *  - Supports automatic PPR calibration (given known target RPM points).
 *  - Supports manual PPR setting (when encoder PPR is known).
 *  - Persists PPR in NVS so calibration survives power cycles.
 *
 * IMPORTANT:
 *  - The sensor signal must be a clean digital pulse at the GPIO (0..3.3V).
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
 *  - Initializes NVS and loads previously saved PPR (if available)
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
 * This is useful for “recalibration” flows triggered by the application.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rpm_counter_recalibrate(void);

/**
 * @brief Manually set the pulses-per-revolution (PPR).
 *
 * Use this if encoder PPR is known (example: 2000 PPR).
 * If persist=true, stores the value into NVS.
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
 * RPM = (pulses_per_second * 60) / PPR
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
