/**
 * @file main.c
 * @brief Sistema integrado: WiFi dinâmico + Sensores Hall + RPM + MQTT + Web Server
 * 
 * Merge das branches sensor_hall e feature-wifi
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "mqtt_client.h"
#include "driver/gpio.h"

// Módulos do projeto
#include "wifi_manager.h"
#include "calibration.h"
#include "web_server.h"
#include "mqtt_send_module.h"
#include "joystick_hall.h"
#include "rpm_pulse_counter.h"

static const char *TAG = "MAIN";

// ============================================================
// CONFIGURAÇÕES
// ============================================================

// MQTT Broker
#define MQTT_BROKER_URI  "mqtt://mqtt.iot.natal.br:1883"
#define MQTT_USER        "desafio05"
#define MQTT_PASS        "desafio05.laica"
#define MQTT_TOPIC_BATCH "ha/desafio05/micael.balza/telemetry_batch"

// Telemetria
#define TELEMETRY_PERIOD_MS  200

// Botões de calibração física
#define BTN_JOY_GPIO        GPIO_NUM_13
#define BTN_RPM_GPIO        GPIO_NUM_12
#define BTN_ACTIVE_LEVEL    0

// ============================================================
// HANDLES GLOBAIS
// ============================================================

static httpd_handle_t g_server = NULL;
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static mqtt_send_module_ctx_t *g_mqtt_send = NULL;
static bool g_mqtt_connected = false;
static bool g_sensors_initialized = false;

// ============================================================
// FUNÇÕES AUXILIARES - BOTÕES
// ============================================================

static void button_init_gpio(gpio_num_t gpio)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
}

static bool button_is_pressed_now(gpio_num_t gpio)
{
    return (gpio_get_level(gpio) == BTN_ACTIVE_LEVEL);
}

static const char* jh_state_str(jh_state_t s)
{
    switch (s) {
        case JH_STATE_NOT_CALIBRATED: return "NOT_CALIB";
        case JH_STATE_MID:            return "MID";
        case JH_STATE_FRONT:          return "FRONT";
        case JH_STATE_BACK:           return "BACK";
        default:                      return "?";
    }
}

// ============================================================
// MQTT EVENT HANDLER
// ============================================================

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "[MQTT] Conectado");
        g_mqtt_connected = true;
        if (g_mqtt_send) mqtt_send_module_set_connected(g_mqtt_send, true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "[MQTT] Desconectado");
        g_mqtt_connected = false;
        if (g_mqtt_send) mqtt_send_module_set_connected(g_mqtt_send, false);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "[MQTT] Erro");
        g_mqtt_connected = false;
        if (g_mqtt_send) mqtt_send_module_set_connected(g_mqtt_send, false);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "[MQTT] Publicado msg_id=%d", event->msg_id);
        if (g_mqtt_send) mqtt_send_module_on_published(g_mqtt_send, event->msg_id);
        break;

    default:
        break;
    }
}

// ============================================================
// INICIALIZAÇÃO DO MQTT
// ============================================================

static void mqtt_init(void)
{
    if (g_mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT já inicializado");
        return;
    }

    ESP_LOGI(TAG, "Inicializando MQTT...");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_mqtt_client) {
        ESP_LOGE(TAG, "Falha ao criar cliente MQTT");
        return;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt_client));
    ESP_LOGI(TAG, "Cliente MQTT iniciado");

    // Inicializa módulo de envio
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
        ESP_LOGE(TAG, "Falha ao inicializar mqtt_send_module");
    }
}

// ============================================================
// INICIALIZAÇÃO DOS SENSORES
// ============================================================

static void sensors_init(void)
{
    if (g_sensors_initialized) {
        ESP_LOGW(TAG, "Sensores já inicializados");
        return;
    }

    ESP_LOGI(TAG, "Inicializando sensores...");

    // Inicializa botões
    button_init_gpio(BTN_JOY_GPIO);
    button_init_gpio(BTN_RPM_GPIO);

    // Verifica se deve forçar recalibração
    bool force_joy_recalib = button_is_pressed_now(BTN_JOY_GPIO);
    bool force_rpm_recalib = button_is_pressed_now(BTN_RPM_GPIO);

    if (force_joy_recalib) {
        ESP_LOGW(TAG, "[JOY] Botão pressionado no boot -> forçar recalibração");
    }
    if (force_rpm_recalib) {
        ESP_LOGW(TAG, "[RPM] Botão pressionado no boot -> forçar recalibração");
    }

    // Configura e inicializa joystick hall
    joystick_hall_config_t joy_cfg = {
        .sensor_a_ch = ADC1_CHANNEL_5, // GPIO33
        .sensor_b_ch = ADC1_CHANNEL_6, // GPIO34
        .width = ADC_WIDTH_BIT_12,
        .atten = ADC_ATTEN_DB_12,
        .samples_per_read = 32,
        .deadband_percent = 5,
    };

    esp_err_t err = joystick_hall_init(&joy_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar joystick_hall: %s", esp_err_to_name(err));
    } else {
        if (force_joy_recalib) {
            joystick_hall_clear_calibration();
        }
        ESP_LOGI(TAG, "[JOY] Inicializado. Calibrado: %s", 
                 joystick_hall_is_calibrated() ? "SIM" : "NAO");
    }

    // Configura e inicializa RPM counter
    RpmCounterConfig rpm_cfg = {
        .encoder_gpio         = GPIO_NUM_32,
        .num_calib_points     = 3,
        .calib_target_rpm     = {1000.0f, 1200.0f, 1500.0f},
        .calib_window_seconds = 3.0f
    };

    err = rpm_counter_init(&rpm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar rpm_counter: %s", esp_err_to_name(err));
    } else {
        if (force_rpm_recalib) {
            rpm_counter_clear_ppr_calibration();
        }
        float ppr = rpm_counter_get_estimated_ppr();
        ESP_LOGI(TAG, "[RPM] Inicializado. PPR: %.2f", ppr);
    }

    g_sensors_initialized = true;
    ESP_LOGI(TAG, "Sensores inicializados");
}

// ============================================================
// TASK DE TELEMETRIA
// ============================================================

static void telemetry_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task de telemetria iniciada (período=%d ms)", TELEMETRY_PERIOD_MS);

    const float rpm_window_s = 0.10f;

    while (1) {
        // Só envia telemetria se WiFi e MQTT estiverem conectados
        if (wifi_manager_get_state() != WIFI_STA_CONNECTED || !g_mqtt_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!g_sensors_initialized || !g_mqtt_send) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        mqtt_send_sample_t s = {0};
        s.ts_ms = esp_timer_get_time() / 1000;

        // Lê joystick
        joystick_hall_reading_t jr;
        esp_err_t err = joystick_hall_read(&jr);
        if (err == ESP_OK) {
            s.front_percent = (uint8_t)jr.front_percent;
            s.back_percent  = (uint8_t)jr.back_percent;
            s.state         = jr.state;
        }

        // Lê RPM
        float rpm = 0.0f;
        err = rpm_counter_measure_rpm_blocking(rpm_window_s, &rpm);
        s.rpm = (err == ESP_OK) ? rpm : 0.0f;

        // Envia
        bool ok = mqtt_send_module_push_sample(g_mqtt_send, &s);

        ESP_LOGD(TAG, "Telemetria: joy=%s f=%u b=%u rpm=%.1f queue=%u",
                 jh_state_str(jr.state),
                 (unsigned)s.front_percent,
                 (unsigned)s.back_percent,
                 (double)s.rpm,
                 (unsigned)mqtt_send_module_count(g_mqtt_send));

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

// ============================================================
// CALLBACKS
// ============================================================

static void wifi_state_changed_callback(wifi_state_t state, const char *ip)
{
    switch (state) {
        case WIFI_AP_MODE:
            ESP_LOGI(TAG, "WiFi em modo AP - Acesse: http://%s", ip);
            break;
        case WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi conectado - IP: %s", ip);
            // Inicializa MQTT quando conectar
            if (g_mqtt_client == NULL) {
                mqtt_init();
            }
            break;
        case WIFI_CONNECTING:
            ESP_LOGI(TAG, "Conectando ao WiFi...");
            break;
        case WIFI_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi desconectado");
            break;
        case WIFI_ERROR:
            ESP_LOGE(TAG, "Erro no WiFi");
            break;
    }
}

static void calibration_state_changed_callback(calibration_state_t state)
{
    ESP_LOGI(TAG, "Estado calibração: %s", calibration_get_state_string(state));
}

// ============================================================
// INIT WEB SERVER
// ============================================================

static void init_web_server(void)
{
    int timeout = 0;
    while (wifi_manager_get_state() != WIFI_AP_MODE && 
           wifi_manager_get_state() != WIFI_STA_CONNECTED && 
           timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }

    if (g_server != NULL) {
        ESP_LOGW(TAG, "Servidor já está rodando");
        return;
    }

    g_server = web_server_init();
    if (g_server == NULL) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor web");
    }
}

// ============================================================
// TASK DE INICIALIZAÇÃO
// ============================================================

static void system_init_task(void *pvParameters)
{
    ESP_LOGI(TAG, "=== Sistema Reversora ESP32 ===");
    ESP_LOGI(TAG, "Inicializando módulos...");

    // NVS (necessário para WiFi e sensores)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS inicializado");

    // Inicializa sensores
    sensors_init();

    // Inicializa calibração (conecta com sensores reais)
    if (calibration_init() == ESP_OK) {
        ESP_LOGI(TAG, "Módulo de calibração inicializado");
        calibration_register_callback(calibration_state_changed_callback);
    }

    // Inicializa WiFi Manager
    if (wifi_manager_init() == ESP_OK) {
        ESP_LOGI(TAG, "WiFi Manager inicializado");
        wifi_manager_register_callback(wifi_state_changed_callback);
    }

    // Aguarda e inicia web server
    vTaskDelay(pdMS_TO_TICKS(1000));
    init_web_server();

    // Cria task de telemetria
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Sistema pronto!");

    vTaskDelete(NULL);
}

// ============================================================
// APP_MAIN
// ============================================================

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando aplicação...");

    xTaskCreate(
        system_init_task,
        "system_init",
        8192,
        NULL,
        5,
        NULL
    );

    // Loop principal - monitoramento
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        wifi_state_t wifi_state = wifi_manager_get_state();
        calibration_state_t calib_state = calibration_get_state();

        // Lê estado dos sensores se inicializados
        const char *joy_status = "N/A";
        float rpm = 0.0f;
        
        if (g_sensors_initialized) {
            joystick_hall_reading_t jr;
            if (joystick_hall_read(&jr) == ESP_OK) {
                joy_status = jh_state_str(jr.state);
            }
            rpm_counter_measure_rpm_blocking(0.1f, &rpm);
        }

        ESP_LOGI(TAG, "Status: WiFi=%s, Calib=%s, Joy=%s, RPM=%.0f, MQTT=%s, IP=%s",
                 (wifi_state == WIFI_AP_MODE ? "AP" : 
                  wifi_state == WIFI_STA_CONNECTED ? "STA" : "DISC"),
                 calibration_get_state_string(calib_state),
                 joy_status,
                 (double)rpm,
                 g_mqtt_connected ? "OK" : "OFF",
                 wifi_manager_get_ip());
    }
}
