#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_wifi.h"
#include "esp_event.h"
// #include "time_sync.h"
#include "DS1302Manager.h"
#include "esp_sntp.h"
#include <sys/time.h>


#include "joystick_hall.h"
#include "rpm_pulse_counter.h"


static const char *TAG = "MAIN";

static bool wifi_connected = false;

static bool ntp_synced = false;


// config wifi
#define WIFI_SSID      "VIVOFIBRA-71C8"
#define WIFI_PASSWORD  "1EEA618025"

// config ntp
#define NTP_SERVER1 "a.ntp.br"
#define NTP_SERVER2 "pool.ntp.org"
#define NTP_SERVER3 "time.google.com"

#define TIMEZONE "BRT3BRST,M10.3.0/0,M2.3.0/0"

#define SYNC_INTERVAL_SEC (60 * 60)

// DS1302 GPIOs
#define DS1302_CLK_GPIO  14
#define DS1302_DAT_GPIO  12
#define DS1302_RST_GPIO  13



// ----------------- Encoder / RPM measurement -----------------
#define RPM_MEASUREMENT_WINDOW_SECONDS   1.0f     // blocking window
#define RPM_PRINT_PERIOD_MS              10000    // print every 10 seconds

// ----------------- Calibration button ------------------------
// TODO: essa GPIO_NUM_13 está sendo usada em dois lugares no codigo. Está correto ou precisa ser mudado?
#define CALIB_BUTTON_GPIO      GPIO_NUM_13   // D13 on your board
#define BUTTON_DEBOUNCE_MS     50           // debounce time
#define BUTTON_LONG_PRESS_MS   3000         // 3 seconds long press

// ----------------- Calibration configuration -----------------
#define CALIB_POINT_COUNT      3

static const float kCalibrationTargetRpm[CALIB_POINT_COUNT] = {
    1000.0f,
    1200.0f,
    1500.0f
};

// Pulses/s measured at each calibration point
static float sCalibrationMeasuredPulsesPerSecond[CALIB_POINT_COUNT] = {0.0f};

// -------------------------------------------------------------
// Wifi config e status // TODO: isso vai mudar com a logica de Access Point
// -------------------------------------------------------------

bool wifi_is_connected(void) {
    return wifi_connected;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
    }
}

void wifi_init_sta(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}


static const char *TAG_TIME = "TIME";

static void time_sync_cb(struct timeval *tv) {
    ESP_LOGI(TAG_TIME, "NTP sincronizado");

    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);

    RTCDateTime dt = {
        .year = timeinfo.tm_year + 1900,
        .month = timeinfo.tm_mon + 1,
        .day = timeinfo.tm_mday,
        .hour = timeinfo.tm_hour,
        .minute = timeinfo.tm_min,
        .second = timeinfo.tm_sec,
        .dayOfWeek = timeinfo.tm_wday
    };

    if (ds1302_set_datetime(&dt)) {
        ESP_LOGI(TAG_TIME, "✓ DS1302 atualizado com horário NTP");
        ntp_synced = true;
    } else {
        ESP_LOGI(TAG_TIME, "✗ Falha ao atualizar DS1302!");
    }

}

void ntp_init(void) {
    setenv("TZ", TIMEZONE, 1);
    tzset();

    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER1);
    esp_sntp_setservername(1, NTP_SERVER2);
    esp_sntp_setservername(2, NTP_SERVER3);
    esp_sntp_init();
}

void sync_system_from_rtc(void) {

    ESP_LOGI(TAG_TIME, "Tentando sincronizar sistema a partir do DS1302...");
    
    // Verifica se o DS1302 tem horário válido
    if (!ds1302_is_valid()) {
        ESP_LOGW(TAG_TIME, "DS1302 não tem horário válido! Usando horário padrão.");
        return;
    }

    RTCDateTime dt = ds1302_get_datetime();

    struct tm tm = {
        .tm_year = dt.year - 1900,
        .tm_mon  = dt.month - 1,
        .tm_mday = dt.day,
        .tm_hour = dt.hour,
        .tm_min  = dt.minute,
        .tm_sec  = dt.second,
        .tm_isdst = -1
    };

    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG_TIME, "Sistema atualizado a partir do RTC");
}

void time_task(void *arg) {
    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        // Monta string de status
        char status[64];
        if (ntp_synced && wifi_connected) {
            snprintf(status, sizeof(status), "NTP+RTC | WiFi: ✓");
        } else if (wifi_connected) {
            snprintf(status, sizeof(status), "RTC | WiFi: ✓ (aguardando NTP)");
        } else {
            snprintf(status, sizeof(status), "RTC | WiFi: ✗");
        }

        printf("[%02d/%02d/%04d %02d:%02d:%02d]\n",
               timeinfo.tm_mday,
               timeinfo.tm_mon + 1,
               timeinfo.tm_year + 1900,
               timeinfo.tm_hour,
               timeinfo.tm_min,
               timeinfo.tm_sec);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



// -------------------------------------------------------------
// Button setup and edge detection
// -------------------------------------------------------------

static void configure_calibration_button(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CALIB_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,     // button to GND
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));
}

/**
 * @brief Detect a debounced falling edge on the calibration button.
 *
 * Button wiring:
 *  - idle: logic HIGH (internal pull-up),
 *  - pressed: logic LOW (connected to GND).
 *
 * @return true if a new button press was detected.
 */
static bool is_calibration_button_pressed_edge(void)
{
    static int      last_level     = 1;
    static uint64_t last_change_us = 0;

    const int current_level = gpio_get_level(CALIB_BUTTON_GPIO);
    const uint64_t now_us   = esp_timer_get_time();

    if (current_level != last_level &&
        (now_us - last_change_us) > (BUTTON_DEBOUNCE_MS * 1000ULL))
    {
        last_change_us = now_us;
        last_level     = current_level;

        // Falling edge: HIGH -> LOW
        if (current_level == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Detect a long press on the calibration button (3 seconds).
 *
 * @return true if the button was pressed for more than 3 seconds.
 */
static bool is_calibration_button_long_pressed(void)
{
    static uint64_t button_press_time = 0;
    const int current_level = gpio_get_level(CALIB_BUTTON_GPIO);

    if (current_level == 0) {
        if (button_press_time == 0) {
            // Button pressed, start timing
            button_press_time = esp_timer_get_time();
        } else if ((esp_timer_get_time() - button_press_time) >= BUTTON_LONG_PRESS_MS * 1000) {
            // Long press detected
            return true;
        }
    } else {
        button_press_time = 0; // Reset time on button release
    }

    return false;
}

// -------------------------------------------------------------
// Calibration flow (interactive)
// -------------------------------------------------------------

static esp_err_t run_interactive_calibration(void)
{
    ESP_LOGI(TAG, "Entering interactive calibration mode (blocking).");
    ESP_LOGI(TAG,
             "Targets: point1=%.0f RPM, point2=%.0f RPM, point3=%.0f RPM.",
             kCalibrationTargetRpm[0],
             kCalibrationTargetRpm[1],
             kCalibrationTargetRpm[2]);

    int current_index = 0;

    while (current_index < CALIB_POINT_COUNT) {
        uint32_t pulses = 0;
        float pulses_per_s = 0.0f;
        float rpm_preview = 0.0f; // may be wrong before PPR is correct; keep just for info if you want

        esp_err_t err = rpm_counter_measure_debug(
            RPM_MEASUREMENT_WINDOW_SECONDS,
            &pulses,
            &pulses_per_s,
            &rpm_preview
        );

        if (err == ESP_OK) {
            float target = kCalibrationTargetRpm[current_index];

            ESP_LOGI(TAG,
                     "[CALIB %d/%d] target=%.1f RPM | pulses=%lu | pulses/s=%.2f",
                     current_index + 1,
                     CALIB_POINT_COUNT,
                     target,
                     (unsigned long)pulses,
                     pulses_per_s);

            ESP_LOGI(TAG, "  -> Set motor to %.1f RPM and PRESS the button to capture.", target);
        } else {
            // If PPR isn't valid yet, measure_debug may fail. In that case, just keep looping.
            // But with your module, it will fail only if PPR <= 0. So we need a temporary PPR.
            // We'll handle that by temporarily setting a non-persistent guess once, below.
        }

        if (is_calibration_button_pressed_edge()) {
            sCalibrationMeasuredPulsesPerSecond[current_index] = pulses_per_s;

            ESP_LOGI(TAG,
                     ">> Captured point %d: target=%.1f RPM, pulses/s=%.2f",
                     current_index + 1,
                     kCalibrationTargetRpm[current_index],
                     pulses_per_s);

            current_index++;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Calibration finished. Computing PPR...");

    float sum_ppr = 0.0f;
    int used_pts = 0;

    for (int i = 0; i < CALIB_POINT_COUNT; i++) {
        float target_rpm   = kCalibrationTargetRpm[i];
        float pulses_per_s = sCalibrationMeasuredPulsesPerSecond[i];

        if (target_rpm > 0.0f && pulses_per_s > 0.0f) {
            float ppr_i = (pulses_per_s * 60.0f) / target_rpm;
            sum_ppr += ppr_i;
            used_pts++;

            ESP_LOGI(TAG,
                     "  Point %d: target=%.1f RPM, pulses/s=%.2f => PPR_estimate=%.3f",
                     i + 1, target_rpm, pulses_per_s, ppr_i);
        } else {
            ESP_LOGW(TAG, "  Point %d invalid (target=%.1f, pulses/s=%.2f) - skipped",
                     i + 1, target_rpm, pulses_per_s);
        }
    }

    if (used_pts == 0) {
        ESP_LOGE(TAG, "No valid calibration samples captured.");
        return ESP_FAIL;
    }

    float final_ppr = sum_ppr / (float)used_pts;

    ESP_LOGI(TAG, "Final calibrated PPR=%.3f (from %d point(s))", final_ppr, used_pts);

    // Persist to NVS (THIS is what you want)
    return rpm_counter_set_estimated_ppr(final_ppr, true);
}


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
    
    ESP_LOGI(TAG, "\n[1/3] Inicializando DS1302...");
    if (!ds1302_begin(DS1302_RST_GPIO, DS1302_CLK_GPIO, DS1302_DAT_GPIO)) {
        ESP_LOGI(TAG, "FALHA CRÍTICA: DS1302 não inicializado!");
        ESP_LOGI(TAG, "Verifique as conexões:");
        ESP_LOGI(TAG, "  RST: GPIO %d", DS1302_RST_GPIO);
        ESP_LOGI(TAG, "  CLK: GPIO %d", DS1302_CLK_GPIO);
        ESP_LOGI(TAG, "  DAT: GPIO %d", DS1302_DAT_GPIO);
        return;
    }

    if (ds1302_is_halted()) {
        ESP_LOGW(TAG, "DS1302 estava parado! Iniciando...");
        ds1302_set_halt(false);
    }

    RTCDateTime rtc_time = ds1302_get_datetime();
    ESP_LOGI(TAG, "Horário atual no DS1302: %02d/%02d/%04d %02d:%02d:%02d",
             rtc_time.day, rtc_time.month, rtc_time.year,
             rtc_time.hour, rtc_time.minute, rtc_time.second);

    // 2. Inicializa WiFi
    ESP_LOGI(TAG, "\n[2/3] Inicializando WiFi...");
    wifi_init_sta();

    ESP_LOGI(TAG, "Aguardando conexão WiFi (timeout: 15s)...");
    int wifi_retry = 0;
    while (!wifi_is_connected() && wifi_retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_retry++;
    }

    vTaskDelay(pdMS_TO_TICKS(3000));

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi conectado! Iniciando NTP...");
        ntp_init();
    } else {
        sync_system_from_rtc();
    }

    xTaskCreate(time_task, "time_task", 4096, NULL, 5, NULL);

    // // ⏳ Aguarda 10s para abrir o Serial Monitor
    // vTaskDelay(pdMS_TO_TICKS(10000));

    // RpmCounterConfig rpm_config = {
    //     .encoder_gpio         = GPIO_NUM_32,
    //     .num_calib_points     = CALIB_POINT_COUNT,
    //     .calib_target_rpm     = {1000.0f, 1200.0f, 1500.0f},
    //     .calib_window_seconds = 3.0f
    // };
    // // TODO: comentei essa linha porque estava redundante
    // // ESP_LOGI(TAG, "Inicializando sistema...");

    // ESP_ERROR_CHECK(rpm_counter_init(&rpm_config));

    // configure_calibration_button();

    // ESP_LOGI(TAG, "System started. Encoder on GPIO32, button on GPIO13 (D13).");

    // // If no PPR was loaded from NVS, force calibration flow.
    // float ppr = rpm_counter_get_estimated_ppr();
    // if (ppr <= 0.0f) {
    //     ESP_LOGW(TAG, "No valid PPR loaded from NVS. Calibration is required.");

    //     // Temporary non-persistent guess so measure_debug can show pulses/s while calibrating
    //     // (any positive value is fine just to allow debug function to run)
    //     ESP_ERROR_CHECK(rpm_counter_set_estimated_ppr(1000.0f, false));

    //     ESP_ERROR_CHECK(run_interactive_calibration());
    //     ppr = rpm_counter_get_estimated_ppr();
    //     ESP_LOGI(TAG, "Calibration stored in NVS. Current PPR=%.3f", ppr);
    // } else {
    //     ESP_LOGI(TAG, "Loaded PPR from NVS: %.3f. Skipping calibration.", ppr);
    // }

    // ESP_LOGI(TAG, "Entering normal monitoring (prints every 10 seconds).");

    // // calibração do sensor hall
    // button_init();

    // // Detecta botão pressionado no BOOT
    // bool force_recalib = false;
    // if (gpio_get_level(BTN_GPIO) == BTN_ACTIVE_LEVEL) {
    //     ESP_LOGW(TAG, "Botao pressionado no boot -> FORCANDO recalibracao");
    //     force_recalib = true;
    // }

    // joystick_hall_config_t cfg = {
    //     .sensor_a_ch = ADC1_CHANNEL_5, // GPIO33
    //     .sensor_b_ch = ADC1_CHANNEL_6, // GPIO34
    //     .width = ADC_WIDTH_BIT_12,
    //     .atten = ADC_ATTEN_DB_12,
    //     .samples_per_read = 32,
    //     .deadband_percent = 5,
    // };

    // ESP_ERROR_CHECK(joystick_hall_init(&cfg));

    // // Força limpeza da calibração se solicitado
    // if (force_recalib) {
    //     ESP_ERROR_CHECK(joystick_hall_clear_calibration());
    // }

    // // ---------------- CALIBRAÇÃO ----------------
    // if (!joystick_hall_is_calibrated()) {

    //     ESP_LOGI(TAG, "===== CALIBRACAO DO MANCHE =====");

    //     ESP_LOGI(TAG, "Coloque o manche no MEIO e aperte o botao.");
    //     button_wait_press();
    //     ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_MID));

    //     ESP_LOGI(TAG, "Coloque o manche para FRENTE e aperte o botao.");
    //     button_wait_press();
    //     ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_FRONT));

    //     ESP_LOGI(TAG, "Coloque o manche para TRAS e aperte o botao.");
    //     button_wait_press();
    //     ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_BACK));

    //     ESP_ERROR_CHECK(joystick_hall_save_calibration());
    //     ESP_LOGI(TAG, "Calibracao concluida e salva no NVS!");
    // } else {
    //     ESP_LOGI(TAG, "Calibracao carregada do NVS.");
    // }

    // ESP_LOGI(TAG, "===== INICIANDO LEITURA DO MANCHE =====");

    // // ---------------- LEITURA ----------------
    // while (1) {
    //     // dados de reversora
    //     joystick_hall_reading_t r;
    //     ESP_ERROR_CHECK(joystick_hall_read(&r));

    //     // dados de rpm
    //     uint32_t pulses = 0;
    //     float pulses_per_s = 0.0f;
    //     float rpm = 0.0f;
    //     esp_err_t err = rpm_counter_measure_debug(
    //         RPM_MEASUREMENT_WINDOW_SECONDS,
    //         &pulses,
    //         &pulses_per_s,
    //         &rpm
    //     );

    //     if (err == ESP_OK) {
    //         printf("RPM DATA: ");
    //         printf("pulses=%lu | pulses/s=%.2f | RPM=%.2f\n",
    //                (unsigned long)pulses, pulses_per_s, rpm);
    //     }

    //     if (is_calibration_button_long_pressed()) {
    //         ESP_LOGI(TAG, "Long press detected. Recalibrating...");
    //         ESP_ERROR_CHECK(rpm_counter_recalibrate());
    //     }


    //     printf("REVERSORA DATA: ");
    //     printf(
    //         "A=%4d  B=%4d  D=%5d | %-5s | frente=%3d%%  tras=%3d%%\n",
    //         r.a_raw,
    //         r.b_raw,
    //         r.d_raw,
    //         state_str(r.state),
    //         r.front_percent,
    //         r.back_percent
    //     );

    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
}
