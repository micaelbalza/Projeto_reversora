#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

#define WIFI_SSID_STORAGE "wifi_ssid"
#define WIFI_PASSWORD_STORAGE "wifi_pass"
#define WIFI_NAMESPACE "wifi_config"
#define WIFI_AP_SSID "ESP32-CALIB"
#define WIFI_AP_PASS "12345678"
#define WIFI_AP_CHANNEL 1
#define WIFI_MAX_CLIENTS 4

static const char *TAG = "WIFI_MANAGER";

static wifi_state_t current_state = WIFI_DISCONNECTED;
static wifi_state_callback_t state_callback = NULL;
static char current_ip[16] = "0.0.0.0";
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static int retry_count = 0;
#define WIFI_MAX_RETRY 5

/**
 * @brief Atualiza estado e chama callback
 */
static void wifi_manager_update_state(wifi_state_t state, const char *ip) {
    current_state = state;
    if (ip) {
        strncpy(current_ip, ip, sizeof(current_ip) - 1);
        current_ip[sizeof(current_ip) - 1] = '\0';
    }
    
    if (state_callback) {
        state_callback(state, current_ip);
    }
    
    switch (state) {
        case WIFI_AP_MODE:
            ESP_LOGI(TAG, "Estado: AP_MODE com IP %s", current_ip);
            break;
        case WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "Estado: STA_CONNECTED com IP %s", current_ip);
            break;
        case WIFI_CONNECTING:
            ESP_LOGI(TAG, "Estado: CONNECTING");
            break;
        case WIFI_DISCONNECTED:
            ESP_LOGI(TAG, "Estado: DISCONNECTED");
            break;
        case WIFI_ERROR:
            ESP_LOGE(TAG, "Estado: ERROR");
            break;
    }
}

/**
 * @brief Handler para eventos Wi-Fi
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Iniciando STA...");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Conectado ao AP");
                retry_count = 0;
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Desconectado do AP");
                if (retry_count < WIFI_MAX_RETRY) {
                    retry_count++;
                    ESP_LOGI(TAG, "Tentando reconectar... (%d/%d)", retry_count, WIFI_MAX_RETRY);
                    wifi_manager_update_state(WIFI_CONNECTING, NULL);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "Máximo de tentativas atingido. Voltando para AP mode");
                    retry_count = 0;
                    esp_wifi_stop();
                    wifi_manager_start_ap();
                }
                break;
                
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Cliente conectado ao AP");
                break;
                
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Cliente desconectado do AP");
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "IP obtido: " IPSTR, IP2STR(&event->ip_info.ip));
            
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            wifi_manager_update_state(WIFI_STA_CONNECTED, ip_str);
        }
    }
}

/**
 * @brief Carrega credenciais da NVS
 */
static esp_err_t wifi_load_credentials(char *ssid, char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS não inicializado, credenciais não encontradas");
        return err;
    }
    
    size_t ssid_len = 32;
    size_t pass_len = 64;
    
    err = nvs_get_str(nvs_handle, WIFI_SSID_STORAGE, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, WIFI_PASSWORD_STORAGE, password, &pass_len);
    }
    
    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Salva credenciais na NVS
 */
static esp_err_t wifi_save_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS");
        return err;
    }
    
    err = nvs_set_str(nvs_handle, WIFI_SSID_STORAGE, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, WIFI_PASSWORD_STORAGE, password);
    }
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Credenciais salvas na NVS");
    } else {
        ESP_LOGE(TAG, "Erro ao salvar credenciais");
    }
    
    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Inicializa o gerenciador de Wi-Fi
 */
esp_err_t wifi_manager_init(void) {
    ESP_LOGI(TAG, "Inicializando gerenciador de Wi-Fi");
    
    // Inicializa NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Inicializa stack de rede
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Cria interfaces de rede
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    
    // Configura Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Registra handler de eventos
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                &wifi_event_handler, NULL));
    
    // Tenta carregar credenciais e conectar STA
    char ssid[32] = {0};
    char password[64] = {0};
    
    if (wifi_load_credentials(ssid, password) == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Credenciais encontradas. Tentando conectar como STA...");
        wifi_manager_start_sta();
    } else {
        ESP_LOGI(TAG, "Credenciais não encontradas. Iniciando modo AP");
        wifi_manager_start_ap();
    }
    
    return ESP_OK;
}

/**
 * @brief Registra callback de estado
 */
esp_err_t wifi_manager_register_callback(wifi_state_callback_t callback) {
    state_callback = callback;
    return ESP_OK;
}

/**
 * @brief Inicia modo STA
 */
esp_err_t wifi_manager_start_sta(void) {
    char ssid[32] = {0};
    char password[64] = {0};
    
    if (wifi_load_credentials(ssid, password) != ESP_OK) {
        ESP_LOGE(TAG, "Credenciais não disponíveis");
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) return ret;
    
    wifi_sta_config_t sta_config = {
        .ssid = {0},
        .password = {0},
    };
    strncpy((char *)sta_config.ssid, ssid, sizeof(sta_config.ssid) - 1);
    strncpy((char *)sta_config.password, password, sizeof(sta_config.password) - 1);
    
    wifi_config_t wifi_config = {
        .sta = sta_config,
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_manager_update_state(WIFI_CONNECTING, NULL);
    return ESP_OK;
}

/**
 * @brief Inicia modo AP
 */
esp_err_t wifi_manager_start_ap(void) {
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) return ret;
    
    wifi_ap_config_t ap_config = {
        .ssid_hidden = 0,
        .max_connection = WIFI_MAX_CLIENTS,
        .authmode = WIFI_AUTH_WPA2_PSK,
    };
    strncpy((char *)ap_config.ssid, WIFI_AP_SSID, sizeof(ap_config.ssid) - 1);
    strncpy((char *)ap_config.password, WIFI_AP_PASS, sizeof(ap_config.password) - 1);
    
    wifi_config_t wifi_config = {
        .ap = ap_config,
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar AP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Define IP fixo para AP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);
    
    wifi_manager_update_state(WIFI_AP_MODE, "192.168.4.1");
    return ESP_OK;
}

/**
 * @brief Conecta STA com credenciais fornecidas
 */
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password) {
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Valida tamanho
    if (strlen(ssid) >= 32 || strlen(password) >= 64) {
        ESP_LOGE(TAG, "SSID ou senha inválidos");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Salva credenciais
    esp_err_t err = wifi_save_credentials(ssid, password);
    if (err != ESP_OK) {
        return err;
    }
    
    // Para modo AP se estiver ativo
    esp_wifi_stop();
    
    // Inicia STA
    return wifi_manager_start_sta();
}

/**
 * @brief Obtém estado atual
 */
wifi_state_t wifi_manager_get_state(void) {
    return current_state;
}

/**
 * @brief Obtém IP atual
 */
const char *wifi_manager_get_ip(void) {
    return current_ip;
}

/**
 * @brief Reseta credenciais e volta ao AP
 */
esp_err_t wifi_manager_reset_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, WIFI_SSID_STORAGE);
        nvs_erase_key(nvs_handle, WIFI_PASSWORD_STORAGE);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    ESP_LOGI(TAG, "Credenciais resetadas");
    esp_wifi_stop();
    return wifi_manager_start_ap();
}
