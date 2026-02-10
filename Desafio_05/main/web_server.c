#include "web_server.h"
#include "wifi_manager.h"
#include "calibration.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";

// Arquivo HTML embarcado (assets)
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

/**
 * @brief Handler para página principal
 */
static esp_err_t handler_index(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /");
    
    int len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, index_html_start, len);
    
    return ESP_OK;
}

/**
 * @brief Handler para API de configuração Wi-Fi
 */
static esp_err_t handler_wifi_config(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/wifi/config");
    
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Corpo da requisição vazio");
        return ESP_FAIL;
    }
    
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON inválido");
        return ESP_FAIL;
    }
    
    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(root, "password");
    
    if (!ssid_item || !pass_item || !cJSON_IsString(ssid_item) || !cJSON_IsString(pass_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID ou senha ausentes");
        return ESP_FAIL;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *password = pass_item->valuestring;
    
    ESP_LOGI(TAG, "Configurando Wi-Fi: SSID=%s", ssid);
    
    // Tenta conectar
    esp_err_t err = wifi_manager_connect_sta(ssid, password);
    cJSON_Delete(root);
    
    // Resposta
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "conectando");
        cJSON_AddStringToObject(response, "message", "Credenciais salvas, conectando...");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao configurar Wi-Fi");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para status do Wi-Fi
 */
static esp_err_t handler_wifi_status(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/wifi/status");
    
    wifi_state_t state = wifi_manager_get_state();
    const char *ip = wifi_manager_get_ip();
    
    const char *state_str;
    switch (state) {
        case WIFI_AP_MODE:
            state_str = "ap_mode";
            break;
        case WIFI_STA_CONNECTED:
            state_str = "sta_connected";
            break;
        case WIFI_CONNECTING:
            state_str = "connecting";
            break;
        default:
            state_str = "disconnected";
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "state", state_str);
    cJSON_AddStringToObject(response, "ip", ip);
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para iniciar calibração
 */
static esp_err_t handler_calib_start(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/calibration/start");
    
    esp_err_t err = calibration_start();
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Calibração iniciada");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao iniciar calibração");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para confirmar neutro
 */
static esp_err_t handler_calib_neutral(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/calibration/neutral");
    
    esp_err_t err = calibration_confirm_neutral();
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Neutro confirmado");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao confirmar neutro");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para confirmar avante
 */
static esp_err_t handler_calib_forward(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/calibration/forward");
    
    esp_err_t err = calibration_confirm_forward();
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Avante confirmado");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao confirmar avante");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para confirmar ré
 */
static esp_err_t handler_calib_reverse(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/calibration/reverse");
    
    esp_err_t err = calibration_confirm_reverse();
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Ré confirmada");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao confirmar ré");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para finalizar calibração
 */
static esp_err_t handler_calib_finish(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/calibration/finish");
    
    esp_err_t err = calibration_finish();
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Calibração concluída");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao finalizar calibração");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para status da calibração
 */
static esp_err_t handler_calib_status(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/calibration/status");
    
    calibration_state_t state = calibration_get_state();
    int progress = calibration_get_progress();
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "state", calibration_get_state_string(state));
    cJSON_AddNumberToObject(response, "progress", progress);
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Handler para resetar credenciais
 */
static esp_err_t handler_wifi_reset(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/wifi/reset");
    
    esp_err_t err = wifi_manager_reset_credentials();
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Voltando para AP mode");
    } else {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Erro ao resetar");
    }
    
    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    free(response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/**
 * @brief Inicializa servidor HTTP
 */
httpd_handle_t web_server_init(void) {
    ESP_LOGI(TAG, "Iniciando servidor HTTP");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.max_open_sockets = 2;
    
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar servidor");
        return NULL;
    }
    
    if (web_server_register_routes(server) != ESP_OK) {
        httpd_stop(server);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Servidor HTTP iniciado com sucesso");
    return server;
}

/**
 * @brief Registra rotas do servidor
 */
esp_err_t web_server_register_routes(httpd_handle_t server) {
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Rota raiz
    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handler_index,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_root);
    
    // API Wi-Fi
    httpd_uri_t uri_wifi_config = {
        .uri = "/api/wifi/config",
        .method = HTTP_POST,
        .handler = handler_wifi_config,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_wifi_config);
    
    httpd_uri_t uri_wifi_status = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = handler_wifi_status,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_wifi_status);
    
    httpd_uri_t uri_wifi_reset = {
        .uri = "/api/wifi/reset",
        .method = HTTP_POST,
        .handler = handler_wifi_reset,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_wifi_reset);
    
    // API Calibração
    httpd_uri_t uri_calib_start = {
        .uri = "/api/calibration/start",
        .method = HTTP_POST,
        .handler = handler_calib_start,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_calib_start);
    
    httpd_uri_t uri_calib_neutral = {
        .uri = "/api/calibration/neutral",
        .method = HTTP_POST,
        .handler = handler_calib_neutral,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_calib_neutral);
    
    httpd_uri_t uri_calib_forward = {
        .uri = "/api/calibration/forward",
        .method = HTTP_POST,
        .handler = handler_calib_forward,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_calib_forward);
    
    httpd_uri_t uri_calib_reverse = {
        .uri = "/api/calibration/reverse",
        .method = HTTP_POST,
        .handler = handler_calib_reverse,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_calib_reverse);
    
    httpd_uri_t uri_calib_finish = {
        .uri = "/api/calibration/finish",
        .method = HTTP_POST,
        .handler = handler_calib_finish,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_calib_finish);
    
    httpd_uri_t uri_calib_status = {
        .uri = "/api/calibration/status",
        .method = HTTP_GET,
        .handler = handler_calib_status,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_calib_status);
    
    ESP_LOGI(TAG, "Rotas registradas com sucesso");
    return ESP_OK;
}

/**
 * @brief Desliga servidor HTTP
 */
esp_err_t web_server_deinit(httpd_handle_t server) {
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return httpd_stop(server);
}
