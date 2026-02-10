#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "calibration.h"
#include "web_server.h"

static const char *TAG = "MAIN";

// Handle do servidor HTTP
static httpd_handle_t server = NULL;

/**
 * @brief Callback para mudanças de estado Wi-Fi
 */
static void wifi_state_changed_callback(wifi_state_t state, const char *ip) {
    switch (state) {
        case WIFI_AP_MODE:
            ESP_LOGI(TAG, "WiFi em modo AP - Acesse: http://%s", ip);
            break;
        case WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi conectado - Acesse: http://%s", ip);
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

/**
 * @brief Callback para mudanças de estado de calibração
 */
static void calibration_state_changed_callback(calibration_state_t state) {
    ESP_LOGI(TAG, "Estado calibração: %s", calibration_get_state_string(state));
}

/**
 * @brief Inicia o servidor HTTP
 */
static void init_web_server(void) {
    // Aguarda conectividade para iniciar servidor
    int timeout = 0;
    while (wifi_manager_get_state() != WIFI_AP_MODE && 
           wifi_manager_get_state() != WIFI_STA_CONNECTED && 
           timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }
    
    if (server != NULL) {
        ESP_LOGW(TAG, "Servidor já está rodando");
        return;
    }
    
    server = web_server_init();
    if (server == NULL) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor web");
    }
}

/**
 * @brief Task de inicialização do sistema
 */
static void system_init_task(void *pvParameters) {
    ESP_LOGI(TAG, "=== Sistema de Calibração ESP32 ===");
    ESP_LOGI(TAG, "Inicializando módulos...");
    
    // Inicializa NVS (necessário para WiFi e storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS inicializado");
    
    // Inicializa módulo de calibração
    if (calibration_init() == ESP_OK) {
        ESP_LOGI(TAG, "Módulo de calibração inicializado");
        calibration_register_callback(calibration_state_changed_callback);
    } else {
        ESP_LOGE(TAG, "Erro ao inicializar calibração");
    }
    
    // Inicializa gerenciador WiFi
    if (wifi_manager_init() == ESP_OK) {
        ESP_LOGI(TAG, "Gerenciador WiFi inicializado");
        wifi_manager_register_callback(wifi_state_changed_callback);
    } else {
        ESP_LOGE(TAG, "Erro ao inicializar WiFi");
    }
    
    // Aguarda e inicia servidor web
    vTaskDelay(pdMS_TO_TICKS(1000));
    init_web_server();
    
    ESP_LOGI(TAG, "Sistema pronto!");
    
    // Deleta task de inicialização
    vTaskDelete(NULL);
}

/**
 * @brief Ponto de entrada da aplicação
 */
void app_main(void) {
    ESP_LOGI(TAG, "Iniciando aplicação...");
    
    // Cria task de inicialização
    xTaskCreate(
        system_init_task,           // Função da task
        "system_init",              // Nome
        4096,                       // Stack size
        NULL,                       // Parâmetro
        5,                          // Prioridade
        NULL                        // Handle
    );
    
    // Loop principal - apenas monitoramento
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        wifi_state_t wifi_state = wifi_manager_get_state();
        calibration_state_t calib_state = calibration_get_state();
        
        ESP_LOGI(TAG, "Status: WiFi=%s, Calib=%s, IP=%s",
                 (wifi_state == WIFI_AP_MODE ? "AP" : 
                  wifi_state == WIFI_STA_CONNECTED ? "STA" : "DISC"),
                 calibration_get_state_string(calib_state),
                 wifi_manager_get_ip());
    }
}
