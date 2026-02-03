#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "joystick_hall.h"

static const char *TAG = "MAIN";

// -------------------- BOTÃO --------------------
#define BTN_GPIO            GPIO_NUM_13
#define BTN_ACTIVE_LEVEL    0   // pull-up: pressionado = 0

static void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
}

static bool button_wait_press(void)
{
    // espera pressionar
    while (gpio_get_level(BTN_GPIO) != BTN_ACTIVE_LEVEL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // debounce

    // espera soltar
    while (gpio_get_level(BTN_GPIO) == BTN_ACTIVE_LEVEL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return true;
}

// -------------------- AUX --------------------
static const char* state_str(jh_state_t s)
{
    switch (s) {
        case JH_STATE_NOT_CALIBRATED: return "NOT_CALIBRATED";
        case JH_STATE_MID:            return "MID";
        case JH_STATE_FRONT:          return "FRONT";
        case JH_STATE_BACK:           return "BACK";
        default:                      return "?";
    }
}

// -------------------- MAIN --------------------
void app_main(void)
{
    // ⏳ Aguarda 10s para abrir o Serial Monitor
    vTaskDelay(pdMS_TO_TICKS(10000));

    ESP_LOGI(TAG, "Inicializando sistema...");

    button_init();

    // Detecta botão pressionado no BOOT
    bool force_recalib = false;
    if (gpio_get_level(BTN_GPIO) == BTN_ACTIVE_LEVEL) {
        ESP_LOGW(TAG, "Botao pressionado no boot -> FORCANDO recalibracao");
        force_recalib = true;
    }

    joystick_hall_config_t cfg = {
        .sensor_a_ch = ADC1_CHANNEL_5, // GPIO33
        .sensor_b_ch = ADC1_CHANNEL_6, // GPIO34
        .width = ADC_WIDTH_BIT_12,
        .atten = ADC_ATTEN_DB_12,
        .samples_per_read = 32,
        .deadband_percent = 5,
    };

    ESP_ERROR_CHECK(joystick_hall_init(&cfg));

    // Força limpeza da calibração se solicitado
    if (force_recalib) {
        ESP_ERROR_CHECK(joystick_hall_clear_calibration());
    }

    // ---------------- CALIBRAÇÃO ----------------
    if (!joystick_hall_is_calibrated()) {

        ESP_LOGI(TAG, "===== CALIBRACAO DO MANCHE =====");

        ESP_LOGI(TAG, "Coloque o manche no MEIO e aperte o botao.");
        button_wait_press();
        ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_MID));

        ESP_LOGI(TAG, "Coloque o manche para FRENTE e aperte o botao.");
        button_wait_press();
        ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_FRONT));

        ESP_LOGI(TAG, "Coloque o manche para TRAS e aperte o botao.");
        button_wait_press();
        ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_BACK));

        ESP_ERROR_CHECK(joystick_hall_save_calibration());
        ESP_LOGI(TAG, "Calibracao concluida e salva no NVS!");
    } else {
        ESP_LOGI(TAG, "Calibracao carregada do NVS.");
    }

    ESP_LOGI(TAG, "===== INICIANDO LEITURA DO MANCHE =====");

    // ---------------- LEITURA ----------------
    while (1) {
        joystick_hall_reading_t r;
        ESP_ERROR_CHECK(joystick_hall_read(&r));

        printf(
            "A=%4d  B=%4d  D=%5d | %-5s | frente=%3d%%  tras=%3d%%\n",
            r.a_raw,
            r.b_raw,
            r.d_raw,
            state_str(r.state),
            r.front_percent,
            r.back_percent
        );

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
