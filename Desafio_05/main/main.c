// main.c
// Integração: Wi-Fi (STA) + MQTT + MQTT Send Module + joystick_hall + rpm_pulse_counter
//
// Melhorias aplicadas:
// - Calibração interativa do RPM agora captura 12 amostras por ponto
// - Ordena as amostras e descarta 1 menor e 1 maior (média das 10 internas)
// - Adiciona tempo de estabilização após o clique (SETTLE_MS)
//
// IMPORTANTE (NVS):
// - Os módulos joystick_hall e rpm_pulse_counter NÃO chamam nvs_flash_init().
// - Portanto, esta main DEVE inicializar NVS logo no começo do app_main().

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_wifi.h"

#include "mqtt_client.h"

#include "driver/gpio.h"

// ---- Seus módulos ----
#include "mqtt_send_module.h"
#include "joystick_hall.h"
#include "rpm_pulse_counter.h"
#include "micro_sd.h"
#include "ds3231.h"

// ============================================================
// 1) CONFIGURAÇÕES GERAIS DO PROJETO
// ============================================================

// -------- Wi-Fi --------
#define WIFI_SSID  "AndroidAPda822"
#define WIFI_PASS  "1s22s22p6"

// -------- MQTT Broker (Natal) --------
#define MQTT_BROKER_URI  "mqtt://mqtt.iot.natal.br:1883"
#define MQTT_USER        "desafio05"
#define MQTT_PASS        "desafio05.laica"

// -------- MQTT Topic do lote --------
#define MQTT_TOPIC_BATCH "ha/desafio05/micael.balza/telemetry_batch"

// -------- Periodicidade da telemetria --------
#define TELEMETRY_PERIOD_MS  200

static const char *TAG = "MAIN_INTEGRATED";

// ============================================================
// 2) HANDLES/CONTEXTOS GLOBAIS
// ============================================================

static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static mqtt_send_module_ctx_t  *g_mqtt_send   = NULL;

// ============================================================
// 3) WIFI: EVENT GROUP PARA SINALIZAR CONEXÃO
// ============================================================
static EventGroupHandle_t s_wifi_ev = NULL;
#define WIFI_CONNECTED_BIT (1 << 0)

// ============================================================
// 4) BOTÕES (DOIS BOTÕES INDEPENDENTES)
// ============================================================
//
// - BTN_JOY_GPIO (GPIO13): joystick
// - BTN_RPM_GPIO (GPIO12): RPM
//
#define BTN_JOY_GPIO        GPIO_NUM_13
#define BTN_RPM_GPIO        GPIO_NUM_12
#define BTN_ACTIVE_LEVEL    0   // pull-up: pressionado = 0

// ponteiro para escrita no cartao SD
FILE *f;

// variavel onde o timestamp sera guardado
struct tm ds_time = {0};

static void button_init_gpio(gpio_num_t gpio)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

static bool button_is_pressed_now(gpio_num_t gpio)
{
    return (gpio_get_level(gpio) == BTN_ACTIVE_LEVEL);
}

// Espera um clique curto (pressiona e solta) com debounce simples
static void button_wait_click(gpio_num_t gpio)
{
    while (!button_is_pressed_now(gpio)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    while (button_is_pressed_now(gpio)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}

static const char* jh_state_str(jh_state_t s)
{
    switch (s) {
        case JH_STATE_NOT_CALIBRATED: return "NOT_CALIBRATED";
        case JH_STATE_MID:            return "MID";
        case JH_STATE_FRONT:          return "FRONT";
        case JH_STATE_BACK:           return "BACK";
        default:                      return "?";
    }
}

// ============================================================
// 5) WIFI: EVENT HANDLER
// ============================================================
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "[WiFi] STA_START -> esp_wifi_connect()");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "[WiFi] DISCONNECTED -> tentando reconectar...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_ev, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "[WiFi] GOT_IP -> conectado!");
        xEventGroupSetBits(s_wifi_ev, WIFI_CONNECTED_BIT);
    }
}

// ============================================================
// 6) WIFI: INIT + CONNECT (STA)
// ============================================================
static void wifi_init_and_connect_sta(void)
{
    s_wifi_ev = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "[WiFi] Aguardando conexão...");
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_ev,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(30000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "[WiFi] Conectado com sucesso!");
    } else {
        ESP_LOGE(TAG, "[WiFi] Timeout para conectar. Verifique SSID/senha/sinal.");
    }
}

// ============================================================
// 7) MQTT: EVENT HANDLER (integra com mqtt_send_module)
// ============================================================
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "[MQTT] CONNECTED");
        if (g_mqtt_send) mqtt_send_module_set_connected(g_mqtt_send, true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "[MQTT] DISCONNECTED");
        if (g_mqtt_send) mqtt_send_module_set_connected(g_mqtt_send, false);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "[MQTT] ERROR");
        if (g_mqtt_send) mqtt_send_module_set_connected(g_mqtt_send, false);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "[MQTT] PUBLISHED msg_id=%d", event->msg_id);
        if (g_mqtt_send) mqtt_send_module_on_published(g_mqtt_send, event->msg_id);
        break;

    default:
        break;
    }
}

// ============================================================
// 8) JOYSTICK: INIT + CALIBRAÇÃO (BOTÃO GPIO13)
// ============================================================
static void joystick_init_with_calibration(void)
{
    button_init_gpio(BTN_JOY_GPIO);

    bool force_recalib = button_is_pressed_now(BTN_JOY_GPIO);
    if (force_recalib) {
        ESP_LOGW(TAG, "[JOY] Botao (GPIO13) pressionado no boot -> resetando calibração do joystick");
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

    if (force_recalib) {
        ESP_ERROR_CHECK(joystick_hall_clear_calibration());
    }

    if (!joystick_hall_is_calibrated()) {
        ESP_LOGI(TAG, "===== CALIBRACAO DO MANCHE (GPIO13) =====");

        ESP_LOGI(TAG, "1) Coloque no MEIO e clique no botao.");
        button_wait_click(BTN_JOY_GPIO);
        ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_MID));

        ESP_LOGI(TAG, "2) Coloque para FRENTE e clique no botao.");
        button_wait_click(BTN_JOY_GPIO);
        ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_FRONT));

        ESP_LOGI(TAG, "3) Coloque para TRAS e clique no botao.");
        button_wait_click(BTN_JOY_GPIO);
        ESP_ERROR_CHECK(joystick_hall_capture_point(JH_POS_BACK));

        ESP_ERROR_CHECK(joystick_hall_save_calibration());
        ESP_LOGI(TAG, "[JOY] Calibracao concluida e salva no NVS!");
    } else {
        ESP_LOGI(TAG, "[JOY] Calibracao carregada do NVS.");
    }
}

// ============================================================
// 9) RPM: CALIBRAÇÃO INTERATIVA ROBUSTA (12 amostras, trim 1+1)
// ============================================================

#define RPM_CALIB_POINT_COUNT  3
static const float kRpmCalibTargets[RPM_CALIB_POINT_COUNT] = { 1000.0f, 1200.0f, 1500.0f };

// Robust capture settings
#define RPM_CAL_SAMPLES        12
#define RPM_CAL_TRIM           1     // descarta 1 menor e 1 maior => 10 internas
#define RPM_CAL_SAMPLE_WINDOW  1.0f  // segundos por amostra
#define RPM_CAL_PREVIEW_WINDOW 0.5f  // preview (antes do clique)
#define RPM_CAL_SETTLE_MS      2000  // espera estabilizar após clique
#define RPM_CAL_GAP_MS         50    // gap entre amostras

static void sort_float_asc(float *a, int n)
{
    for (int i = 1; i < n; i++) {
        float key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

static esp_err_t rpm_capture_pulses_per_s_trimmed(float *out_pps)
{
    if (!out_pps) return ESP_ERR_INVALID_ARG;

    float samples[RPM_CAL_SAMPLES] = {0};

    for (int k = 0; k < RPM_CAL_SAMPLES; k++) {
        uint32_t pulses = 0;
        float pps = 0.0f;
        float rpm_preview = 0.0f;

        esp_err_t err = rpm_counter_measure_debug(RPM_CAL_SAMPLE_WINDOW, &pulses, &pps, &rpm_preview);
        if (err == ESP_OK && pps > 0.0f) {
            samples[k] = pps;
        } else {
            samples[k] = 0.0f;
        }

        vTaskDelay(pdMS_TO_TICKS(RPM_CAL_GAP_MS));
    }

    sort_float_asc(samples, RPM_CAL_SAMPLES);

    const int lo = RPM_CAL_TRIM;
    const int hi = RPM_CAL_SAMPLES - RPM_CAL_TRIM; // exclusivo
    if (hi <= lo) return ESP_FAIL;

    float sum = 0.0f;
    int n = 0;
    for (int i = lo; i < hi; i++) {
        sum += samples[i];
        n++;
    }
    if (n <= 0) return ESP_FAIL;

    *out_pps = sum / (float)n;
    return ESP_OK;
}

static esp_err_t rpm_run_interactive_calibration(void)
{
    ESP_LOGI(TAG, "===== CALIBRACAO DO RPM (GPIO%d) =====", (int)BTN_RPM_GPIO);

    // Para o measure_debug funcionar, o módulo exige PPR>0.
    // Usamos um "chute" temporário (não persiste) apenas para gerar rpm_preview.
    ESP_ERROR_CHECK(rpm_counter_set_estimated_ppr(1000.0f, false));

    float measured_pulses_per_s[RPM_CALIB_POINT_COUNT] = {0};

    for (int i = 0; i < RPM_CALIB_POINT_COUNT; i++) {
        float target = kRpmCalibTargets[i];

        ESP_LOGI(TAG,
                 "[RPM CAL] Ponto %d/%d: ajuste o motor para %.0f RPM e clique para CAPTURAR.",
                 i + 1, RPM_CALIB_POINT_COUNT, (double)target);

        // Preview ao vivo até clicar
        while (1) {
            uint32_t pulses = 0;
            float pps = 0.0f;
            float rpm_preview = 0.0f;

            esp_err_t err = rpm_counter_measure_debug(RPM_CAL_PREVIEW_WINDOW, &pulses, &pps, &rpm_preview);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "[RPM CAL] alvo=%.0f | pulses=%lu | pulses/s=%.2f | rpm_preview=%.1f",
                         (double)target,
                         (unsigned long)pulses,
                         (double)pps,
                         (double)rpm_preview);
            } else {
                ESP_LOGW(TAG, "[RPM CAL] measure_debug falhou (%s).", esp_err_to_name(err));
            }

            if (button_is_pressed_now(BTN_RPM_GPIO)) {
                button_wait_click(BTN_RPM_GPIO);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI(TAG, "[RPM CAL] Aguardando estabilizar (%d ms)...", RPM_CAL_SETTLE_MS);
        vTaskDelay(pdMS_TO_TICKS(RPM_CAL_SETTLE_MS));

        float pps_trimmed = 0.0f;
        esp_err_t cerr = rpm_capture_pulses_per_s_trimmed(&pps_trimmed);
        if (cerr != ESP_OK || pps_trimmed <= 0.0f) {
            ESP_LOGE(TAG, "[RPM CAL] Falha ao capturar ponto %d (trimmed mean).", i + 1);
            measured_pulses_per_s[i] = 0.0f;
        } else {
            measured_pulses_per_s[i] = pps_trimmed;
            ESP_LOGI(TAG, ">> CAPTURADO ponto %d: alvo=%.0f RPM, pulses/s(trim)=%.2f",
                     i + 1, (double)target, (double)pps_trimmed);
        }
    }

    // Calcula PPR médio:
    float sum_ppr = 0.0f;
    int used = 0;

    ESP_LOGI(TAG, "[RPM CAL] Computando PPR...");
    for (int i = 0; i < RPM_CALIB_POINT_COUNT; i++) {
        float target = kRpmCalibTargets[i];
        float pps = measured_pulses_per_s[i];

        if (target > 0.0f && pps > 0.0f) {
            float ppr_i = (pps * 60.0f) / target;
            sum_ppr += ppr_i;
            used++;

            ESP_LOGI(TAG, "  Ponto %d: alvo=%.0f | pulses/s(trim)=%.2f => PPR=%.3f",
                     i + 1, (double)target, (double)pps, (double)ppr_i);
        } else {
            ESP_LOGW(TAG, "  Ponto %d inválido (alvo=%.0f, pps=%.2f) - ignorado",
                     i + 1, (double)target, (double)pps);
        }
    }

    if (used == 0) {
        ESP_LOGE(TAG, "[RPM CAL] Nenhum ponto válido capturado. Falha de calibração.");
        return ESP_FAIL;
    }

    float final_ppr = sum_ppr / (float)used;
    ESP_LOGI(TAG, "[RPM CAL] PPR final=%.3f (media de %d ponto(s)) -> salvando no NVS",
             (double)final_ppr, used);

    return rpm_counter_set_estimated_ppr(final_ppr, true);
}

// ============================================================
// 10) RPM: INIT + PPR (com botão dedicado)
// ============================================================
static void rpm_init_with_button_calibration(void)
{
    button_init_gpio(BTN_RPM_GPIO);

    bool force_recalib = button_is_pressed_now(BTN_RPM_GPIO);
    if (force_recalib) {
        ESP_LOGW(TAG, "[RPM] Botao (GPIO%d) pressionado no boot -> resetando calibração do RPM",
                 (int)BTN_RPM_GPIO);
    }

    RpmCounterConfig rpm_cfg = {
        .encoder_gpio         = GPIO_NUM_32,
        .num_calib_points     = 3,
        .calib_target_rpm     = {1000.0f, 1200.0f, 1500.0f},
        .calib_window_seconds = 3.0f
    };

    ESP_ERROR_CHECK(rpm_counter_init(&rpm_cfg));

    if (force_recalib) {
        ESP_ERROR_CHECK(rpm_counter_clear_ppr_calibration());
    }

    float ppr = rpm_counter_get_estimated_ppr();

    if (ppr <= 0.0f) {
        ESP_LOGW(TAG, "[RPM] Sem PPR válido no NVS. Calibração interativa será executada.");
        ESP_ERROR_CHECK(rpm_run_interactive_calibration());

        ppr = rpm_counter_get_estimated_ppr();
        ESP_LOGI(TAG, "[RPM] Calibração concluída. PPR atual=%.3f", (double)ppr);
    } else {
        ESP_LOGI(TAG, "[RPM] PPR carregado do NVS: %.3f", (double)ppr);
    }
}

// ============================================================
// 11) TASK: LEITURA REAL (joystick + rpm) E PUSH NO MQTT SEND MODULE
// ============================================================
static void telemetry_real_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Telemetry REAL task started (period=%d ms)", TELEMETRY_PERIOD_MS);

    const float rpm_window_s = 0.10f;
    

    while (1) {
        mqtt_send_sample_t s = {0};
        
        s.ts_ms = esp_timer_get_time() / 1000;

        joystick_hall_reading_t jr;
        ESP_ERROR_CHECK(joystick_hall_read(&jr));

        s.front_percent = (uint8_t)jr.front_percent;
        s.back_percent  = (uint8_t)jr.back_percent;
        s.state         = jr.state;

        float rpm = 0.0f;
        esp_err_t rerr = rpm_counter_measure_rpm_blocking(rpm_window_s, &rpm);
        s.rpm = (rerr == ESP_OK) ? rpm : 0.0f;

        char time_str[64];  // tamanho suficiente para "YYYY-MM-DD HH:MM:SS"

        if (ds3231_get_time(&ds_time) == ESP_OK) {
            snprintf(
                time_str,
                sizeof(time_str),
                "%04d-%02d-%02d %02d:%02d:%02d",
                ds_time.tm_year + 1900,
                ds_time.tm_mon  + 1,
                ds_time.tm_mday,
                ds_time.tm_hour,
                ds_time.tm_min,
                ds_time.tm_sec
            );

        } else {
            ESP_LOGE(TAG, "Falha ao ler DS3231! Verifique conexão I2C");
        }

        // monta o JSON e escreve no SD
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ts\":%lld,\"front\":%u,\"back\":%u,\"state\":%d,\"rpm\":%.2f,\"timestamp\":%s}\n",
            (long long)s.ts_ms,
            s.front_percent,
            s.back_percent,
            s.state,
            s.rpm,
            time_str
        );
        if (!write_sd_card(&f, buf)) {
            ESP_LOGW(TAG, "Falha ao escrever no SD");
        }

        bool ok = mqtt_send_module_push_sample(g_mqtt_send, &s);

        ESP_LOGI(TAG,
                 "push ok=%d | ts=%lld | joy=%s f=%u b=%u | rpm=%.2f | queue=%u",
                 (int)ok,
                 (long long)s.ts_ms,
                 jh_state_str(jr.state),
                 (unsigned)s.front_percent,
                 (unsigned)s.back_percent,
                 (double)s.rpm,
                 (unsigned)mqtt_send_module_count(g_mqtt_send));

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

// ============================================================
// 12) app_main
// ============================================================
void app_main(void)
{
    esp_err_t ret;

    // configuração de montagem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // Evita formatar o cartão por erro de fiação
        .max_files = 5,
        .allocation_unit_size = 0
    };

    // configuração de barramento SPI
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        
    // velocidade reduzida para 200kHz (padrão de inicialização segura)
    host.max_freq_khz = 200; 

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t) PIN_NUM_CS;
    slot_config.host_id = host.slot;

    sdmmc_card_t *card;

    ret = setup_sd_card(host, bus_cfg);

    if (ret != ESP_OK) {
        while (true) {
            printf("Erro ao inicilizar o SPI!\n");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
    
    while (!mount_sd_card(ret, host, slot_config, mount_config, &card)) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    while (!open_sd_card(&f)) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "Starting integrated system (Wi-Fi + MQTT + sensors)...");

    wifi_init_and_connect_sta();

    // funcao de sincronizacao com servidor ntp
    ntp_sync_and_save_to_ds3231();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_mqtt_client) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt_client));
    ESP_LOGI(TAG, "MQTT client started.");

    mqtt_send_module_config_t send_cfg = {
        .topic = MQTT_TOPIC_BATCH,
        .capacity_items = 256,
        .batch_items = 10,
        .payload_bytes_max = 2048,
        .flush_period_ms = 5000,
        .qos = 1,
        .retain = 0,
        .drop_oldest_on_full = true,
        .json_array_mode = true,
        .task_name = "mqtt_send",
        .task_stack_words = 4096,
        .task_priority = 5,
        .task_core = -1
    };

    g_mqtt_send = mqtt_send_module_init(&send_cfg, g_mqtt_client);
    if (!g_mqtt_send) {
        ESP_LOGE(TAG, "Failed to init mqtt_send_module");
        return;
    }

    joystick_init_with_calibration();
    rpm_init_with_button_calibration();

    xTaskCreate(telemetry_real_task, "telemetry_real", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Setup done. Watch MQTT Explorer topic: %s", MQTT_TOPIC_BATCH);
}
