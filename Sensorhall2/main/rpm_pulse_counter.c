#include "rpm_pulse_counter.h"
#include "rpm_pulse_counter.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#define RPM_COUNTER_LOG_TAG           "RPM_COUNTER"
#define RPM_COUNTER_MIN_INTERVAL_US   150  // glitch filter: minimum time between pulses (us)

// NVS storage
#define RPM_COUNTER_NVS_NAMESPACE     "rpm_counter"
#define RPM_COUNTER_NVS_KEY_PPR       "ppr"

// ------------------------ Internal module state ------------------------

static RpmCounterConfig   s_config;
static volatile uint32_t  s_pulse_counter = 0;
static volatile uint64_t  s_last_pulse_us = 0;
static float              s_estimated_ppr = 0.0f;  // calibrated or manually set

// ------------------------ Internal helpers -----------------------------

/**
 * @brief GPIO ISR: counts rising edges with glitch filter.
 */
static void IRAM_ATTR rpm_counter_gpio_isr(void *arg)
{
    gpio_num_t gpio = (gpio_num_t)(intptr_t)arg;
    uint64_t now_us = esp_timer_get_time();

    if ((now_us - s_last_pulse_us) >= RPM_COUNTER_MIN_INTERVAL_US) {
        // confirm level is HIGH to reduce spikes
        if (gpio_get_level(gpio) == 1) {
            s_pulse_counter++;
            s_last_pulse_us = now_us;
        }
    }
}

/**
 * @brief Ensure NVS is initialized.
 */
static esp_err_t rpm_counter_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(RPM_COUNTER_LOG_TAG, "NVS needs erase (no free pages / new version). Erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/**
 * @brief Save PPR to NVS.
 */
static esp_err_t rpm_counter_save_ppr_to_nvs(float ppr)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(RPM_COUNTER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    // store float as blob (safe across IDF versions)
    err = nvs_set_blob(handle, RPM_COUNTER_NVS_KEY_PPR, &ppr, sizeof(float));
    if (err != ESP_OK) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "NVS set failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(RPM_COUNTER_LOG_TAG, "PPR %.3f saved to NVS", ppr);
    } else {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }

    return err;
}

/**
 * @brief Load PPR from NVS. Returns ESP_OK if found and valid.
 */
static esp_err_t rpm_counter_load_ppr_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(RPM_COUNTER_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    float ppr = 0.0f;
    size_t size = sizeof(float);

    err = nvs_get_blob(handle, RPM_COUNTER_NVS_KEY_PPR, &ppr, &size);
    nvs_close(handle);

    if (err == ESP_OK && size == sizeof(float) && ppr > 0.0f) {
        s_estimated_ppr = ppr;
        ESP_LOGI(RPM_COUNTER_LOG_TAG, "Loaded PPR %.3f from NVS", ppr);
        return ESP_OK;
    }

    return ESP_FAIL;
}

/**
 * @brief Measure pulses/second over a blocking time window.
 */
static esp_err_t rpm_counter_measure_pulses_per_second_blocking(float window_seconds,
                                                                float *out_pulses_per_s)
{
    if (window_seconds <= 0.0f || out_pulses_per_s == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t start_count = s_pulse_counter;
    uint64_t t0_us       = esp_timer_get_time();

    int delay_ms = (int)(window_seconds * 1000.0f);
    if (delay_ms <= 0) {
        delay_ms = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    uint32_t end_count = s_pulse_counter;
    uint64_t t1_us     = esp_timer_get_time();

    uint32_t delta_pulses = end_count - start_count;
    double dt_s = (t1_us - t0_us) / 1e6;

    if (dt_s <= 0.0) {
        return ESP_FAIL;
    }

    *out_pulses_per_s = (float)(delta_pulses / dt_s);
    return ESP_OK;
}

// ------------------------ Public API -----------------------------------

esp_err_t rpm_counter_init(const RpmCounterConfig *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->num_calib_points <= 0 || config->num_calib_points > RPM_COUNTER_MAX_CALIB_POINTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->calib_window_seconds <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    // Configure encoder GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << s_config.encoder_gpio,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_POSEDGE
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Install ISR service (ignore "already installed")
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(s_config.encoder_gpio,
                              rpm_counter_gpio_isr,
                              (void *)(intptr_t)s_config.encoder_gpio);
    if (err != ESP_OK) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(err));
        return err;
    }

    s_pulse_counter = 0;
    s_last_pulse_us = esp_timer_get_time();
    s_estimated_ppr = 0.0f;

    // Init NVS and try to load saved PPR
    err = rpm_counter_init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (rpm_counter_load_ppr_from_nvs() != ESP_OK) {
        // Only one message here (no spam)
        ESP_LOGW(RPM_COUNTER_LOG_TAG,
                 "No saved PPR found in NVS. Calibration or manual PPR is required.");
    }

    ESP_LOGI(RPM_COUNTER_LOG_TAG, "Initialized on GPIO %d, current PPR=%.3f",
             (int)s_config.encoder_gpio, s_estimated_ppr);

    return ESP_OK;
}

esp_err_t rpm_counter_run_full_calibration(void)
{
    float sum_ppr = 0.0f;
    int used_points = 0;

    for (int i = 0; i < s_config.num_calib_points; i++) {
        float target_rpm = s_config.calib_target_rpm[i];
        if (target_rpm <= 0.0f) {
            continue;
        }

        ESP_LOGI(RPM_COUNTER_LOG_TAG,
                 "Calibration point %d/%d: target RPM=%.1f (hold speed now)",
                 i + 1, s_config.num_calib_points, target_rpm);

        float pulses_per_s = 0.0f;
        esp_err_t err = rpm_counter_measure_pulses_per_second_blocking(s_config.calib_window_seconds,
                                                                       &pulses_per_s);
        if (err != ESP_OK || pulses_per_s <= 0.0f) {
            ESP_LOGE(RPM_COUNTER_LOG_TAG, "Failed to measure pulses/s at target RPM=%.1f", target_rpm);
            continue;
        }

        float ppr_estimate = (pulses_per_s * 60.0f) / target_rpm;

        ESP_LOGI(RPM_COUNTER_LOG_TAG,
                 "pulses/s=%.3f => PPR_estimate=%.3f",
                 pulses_per_s, ppr_estimate);

        sum_ppr += ppr_estimate;
        used_points++;
    }

    if (used_points == 0) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "No valid calibration points captured.");
        return ESP_FAIL;
    }

    float final_ppr = sum_ppr / (float)used_points;

    ESP_LOGI(RPM_COUNTER_LOG_TAG, "Final calibrated PPR=%.3f (from %d point(s))",
             final_ppr, used_points);

    // Persist in NVS
    return rpm_counter_set_estimated_ppr(final_ppr, true);
}

esp_err_t rpm_counter_clear_ppr_calibration(void)
{
    s_estimated_ppr = 0.0f;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(RPM_COUNTER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, RPM_COUNTER_NVS_KEY_PPR);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(RPM_COUNTER_LOG_TAG, "NVS erase failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(RPM_COUNTER_LOG_TAG, "PPR calibration cleared from NVS");
    }
    return err;
}

esp_err_t rpm_counter_recalibrate(void)
{
    // API “clean”: primeiro limpa, depois roda calibração completa
    esp_err_t err = rpm_counter_clear_ppr_calibration();
    if (err != ESP_OK) {
        return err;
    }
    return rpm_counter_run_full_calibration();
}

esp_err_t rpm_counter_set_estimated_ppr(float ppr, bool persist)
{
    if (ppr <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    s_estimated_ppr = ppr;
    ESP_LOGI(RPM_COUNTER_LOG_TAG, "Estimated PPR set to %.3f", s_estimated_ppr);

    if (persist) {
        return rpm_counter_save_ppr_to_nvs(ppr);
    }
    return ESP_OK;
}

float rpm_counter_get_estimated_ppr(void)
{
    return s_estimated_ppr;
}

esp_err_t rpm_counter_measure_rpm_blocking(float measurement_window_seconds,
                                           float *out_rpm)
{
    if (out_rpm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (measurement_window_seconds <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_estimated_ppr <= 0.0f) {
        // No spam here: return FAIL silently (main decides what to print)
        return ESP_FAIL;
    }

    float pulses_per_s = 0.0f;
    esp_err_t err = rpm_counter_measure_pulses_per_second_blocking(measurement_window_seconds,
                                                                   &pulses_per_s);
    if (err != ESP_OK) {
        return err;
    }

    *out_rpm = (pulses_per_s * 60.0f) / s_estimated_ppr;
    return ESP_OK;
}

esp_err_t rpm_counter_measure_debug(float measurement_window_seconds,
                                    uint32_t *out_pulses,
                                    float *out_pulses_per_second,
                                    float *out_rpm)
{
    if (out_pulses == NULL || out_pulses_per_second == NULL || out_rpm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (measurement_window_seconds <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_estimated_ppr <= 0.0f) {
        return ESP_FAIL;
    }

    uint32_t start_count = s_pulse_counter;
    uint64_t t0_us       = esp_timer_get_time();

    int delay_ms = (int)(measurement_window_seconds * 1000.0f);
    if (delay_ms <= 0) {
        delay_ms = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    uint32_t end_count = s_pulse_counter;
    uint64_t t1_us     = esp_timer_get_time();

    uint32_t delta_pulses = end_count - start_count;
    double dt_s = (t1_us - t0_us) / 1e6;
    if (dt_s <= 0.0) {
        return ESP_FAIL;
    }

    float pulses_per_s = (float)(delta_pulses / dt_s);
    float rpm = (pulses_per_s * 60.0f) / s_estimated_ppr;

    *out_pulses = delta_pulses;
    *out_pulses_per_second = pulses_per_s;
    *out_rpm = rpm;

    return ESP_OK;
}
