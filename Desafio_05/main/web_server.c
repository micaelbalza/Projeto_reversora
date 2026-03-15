#include "web_server.h"
#include "wifi_manager.h"
#include "calibration.h"
#include "rpm_pulse_counter.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";

#define MOTOR_CALIB_POINT_COUNT 3
#define MOTOR_CAL_SAMPLES 6
#define MOTOR_CAL_TRIM 1
#define MOTOR_CAL_SAMPLE_WINDOW_S 0.35f
#define MOTOR_CAL_SETTLE_MS 600
#define MOTOR_CAL_GAP_MS 50

static const float s_motor_calib_targets[MOTOR_CALIB_POINT_COUNT] = {1000.0f, 1200.0f, 1500.0f};

typedef enum {
    MOTOR_CALIB_IDLE = 0,
    MOTOR_CALIB_WAIT_POINT,
    MOTOR_CALIB_CAPTURING,
    MOTOR_CALIB_READY_TO_FINISH,
    MOTOR_CALIB_DONE,
    MOTOR_CALIB_ERROR
} motor_calib_state_t;

typedef struct {
    motor_calib_state_t state;
    int current_point;
    float measured_pps[MOTOR_CALIB_POINT_COUNT];
    float final_ppr;
    char message[128];
} motor_calib_session_t;

static motor_calib_session_t s_motor_calib = {
    .state = MOTOR_CALIB_IDLE,
    .current_point = 0,
    .measured_pps = {0},
    .final_ppr = 0.0f,
    .message = ""
};

static TaskHandle_t s_motor_capture_task = NULL;

static esp_err_t motor_capture_pulses_per_s_trimmed(float *out_pps);

static void motor_sort_float_asc(float *a, int n)
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

static const char *motor_calib_state_str(motor_calib_state_t state)
{
    switch (state) {
        case MOTOR_CALIB_IDLE: return "idle";
        case MOTOR_CALIB_WAIT_POINT: return "wait_point";
        case MOTOR_CALIB_CAPTURING: return "capturing";
        case MOTOR_CALIB_READY_TO_FINISH: return "ready_to_finish";
        case MOTOR_CALIB_DONE: return "done";
        case MOTOR_CALIB_ERROR: return "error";
        default: return "unknown";
    }
}

static void motor_calib_capture_task(void *arg)
{
    (void)arg;

    int point_index = s_motor_calib.current_point;
    if (point_index < 0 || point_index >= MOTOR_CALIB_POINT_COUNT) {
        s_motor_calib.state = MOTOR_CALIB_ERROR;
        snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Índice de ponto inválido");
        s_motor_capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    float target = s_motor_calib_targets[point_index];
    ESP_LOGI(TAG, "[MOTOR CAL] Capturando ponto %d/%d (target=%.0f RPM)",
             point_index + 1, MOTOR_CALIB_POINT_COUNT, (double)target);

    vTaskDelay(pdMS_TO_TICKS(MOTOR_CAL_SETTLE_MS));

    float pps_trimmed = 0.0f;
    esp_err_t err = motor_capture_pulses_per_s_trimmed(&pps_trimmed);

    if (err != ESP_OK || pps_trimmed <= 0.0f) {
        s_motor_calib.state = MOTOR_CALIB_ERROR;
        snprintf(s_motor_calib.message, sizeof(s_motor_calib.message),
                 "Falha na captura do ponto %d", point_index + 1);
    } else {
        s_motor_calib.measured_pps[point_index] = pps_trimmed;
        s_motor_calib.current_point = point_index + 1;

        if (s_motor_calib.current_point >= MOTOR_CALIB_POINT_COUNT) {
            s_motor_calib.state = MOTOR_CALIB_READY_TO_FINISH;
            snprintf(s_motor_calib.message, sizeof(s_motor_calib.message),
                     "Pontos capturados. Clique em concluir");
        } else {
            s_motor_calib.state = MOTOR_CALIB_WAIT_POINT;
            snprintf(s_motor_calib.message, sizeof(s_motor_calib.message),
                     "Ponto %d capturado. Próximo alvo: %.0f RPM",
                     point_index + 1,
                     (double)s_motor_calib_targets[s_motor_calib.current_point]);
        }
    }

    s_motor_capture_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t motor_capture_pulses_per_s_trimmed(float *out_pps)
{
    if (!out_pps) {
        return ESP_ERR_INVALID_ARG;
    }

    float samples[MOTOR_CAL_SAMPLES] = {0};

    for (int index = 0; index < MOTOR_CAL_SAMPLES; index++) {
        uint32_t pulses = 0;
        float pps = 0.0f;
        float rpm_preview = 0.0f;

        esp_err_t err = rpm_counter_measure_debug(MOTOR_CAL_SAMPLE_WINDOW_S, &pulses, &pps, &rpm_preview);
        if (err == ESP_OK && pps > 0.0f) {
            samples[index] = pps;
        } else {
            samples[index] = 0.0f;
        }

        vTaskDelay(pdMS_TO_TICKS(MOTOR_CAL_GAP_MS));
    }

    motor_sort_float_asc(samples, MOTOR_CAL_SAMPLES);

    int first = MOTOR_CAL_TRIM;
    int last = MOTOR_CAL_SAMPLES - MOTOR_CAL_TRIM;
    if (last <= first) {
        return ESP_FAIL;
    }

    float sum = 0.0f;
    int valid_count = 0;
    for (int index = first; index < last; index++) {
        sum += samples[index];
        valid_count++;
    }

    if (valid_count <= 0) {
        return ESP_FAIL;
    }

    *out_pps = sum / (float)valid_count;
    return ESP_OK;
}

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
 * @brief Handler para iniciar calibração do motor (RPM)
 */
static esp_err_t handler_motor_calib_start(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/motor-calibration/start");

    cJSON *response = cJSON_CreateObject();

    if (s_motor_capture_task != NULL) {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "state", motor_calib_state_str(s_motor_calib.state));
        cJSON_AddStringToObject(response, "message", "Aguarde a captura em andamento terminar");

        char *response_str = cJSON_Print(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response_str, strlen(response_str));
        free(response_str);
        cJSON_Delete(response);
        return ESP_OK;
    }

    memset(&s_motor_calib, 0, sizeof(s_motor_calib));
    s_motor_calib.state = MOTOR_CALIB_WAIT_POINT;
    snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Calibração do motor iniciada");

    esp_err_t err = rpm_counter_set_estimated_ppr(1000.0f, false);
    if (err != ESP_OK) {
        s_motor_calib.state = MOTOR_CALIB_ERROR;
        snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Falha ao preparar motor");
    }

    cJSON_AddStringToObject(response, "status", (err == ESP_OK) ? "ok" : "erro");
    cJSON_AddStringToObject(response, "state", motor_calib_state_str(s_motor_calib.state));
    cJSON_AddStringToObject(response, "message", s_motor_calib.message);

    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);

    return ESP_OK;
}

/**
 * @brief Handler para capturar ponto da calibração do motor
 */
static esp_err_t handler_motor_calib_capture(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/motor-calibration/capture");

    cJSON *response = cJSON_CreateObject();

    if (s_motor_calib.state == MOTOR_CALIB_CAPTURING || s_motor_capture_task != NULL) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Captura já em andamento");
    } else if (s_motor_calib.state != MOTOR_CALIB_WAIT_POINT || s_motor_calib.current_point >= MOTOR_CALIB_POINT_COUNT) {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Calibração do motor não está aguardando captura");
    } else {
        s_motor_calib.state = MOTOR_CALIB_CAPTURING;
        float target = s_motor_calib_targets[s_motor_calib.current_point];
        snprintf(s_motor_calib.message, sizeof(s_motor_calib.message),
                 "Capturando ponto %d/%d (alvo %.0f RPM)...",
                 s_motor_calib.current_point + 1,
                 MOTOR_CALIB_POINT_COUNT,
                 (double)target);

        BaseType_t task_created = xTaskCreate(
            motor_calib_capture_task,
            "motor_calib_capture",
            4096,
            NULL,
            4,
            &s_motor_capture_task);

        if (task_created != pdPASS) {
            s_motor_calib.state = MOTOR_CALIB_ERROR;
            s_motor_capture_task = NULL;
            snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Falha ao iniciar task de captura");

            cJSON_AddStringToObject(response, "status", "erro");
            cJSON_AddStringToObject(response, "message", s_motor_calib.message);
        } else {
            cJSON_AddStringToObject(response, "status", "ok");
            cJSON_AddStringToObject(response, "message", s_motor_calib.message);
        }
    }

    cJSON_AddStringToObject(response, "state", motor_calib_state_str(s_motor_calib.state));

    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);

    return ESP_OK;
}

/**
 * @brief Handler para finalizar calibração do motor
 */
static esp_err_t handler_motor_calib_finish(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/motor-calibration/finish");

    cJSON *response = cJSON_CreateObject();

    if (s_motor_calib.state == MOTOR_CALIB_CAPTURING || s_motor_capture_task != NULL) {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Aguarde a captura atual terminar");
    } else if (s_motor_calib.state != MOTOR_CALIB_READY_TO_FINISH) {
        cJSON_AddStringToObject(response, "status", "erro");
        cJSON_AddStringToObject(response, "message", "Calibração do motor ainda não concluiu captura dos pontos");
    } else {
        float sum_ppr = 0.0f;
        int used = 0;

        for (int index = 0; index < MOTOR_CALIB_POINT_COUNT; index++) {
            float target = s_motor_calib_targets[index];
            float pps = s_motor_calib.measured_pps[index];
            if (target > 0.0f && pps > 0.0f) {
                sum_ppr += (pps * 60.0f) / target;
                used++;
            }
        }

        if (used <= 0) {
            s_motor_calib.state = MOTOR_CALIB_ERROR;
            snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Nenhum ponto válido para calcular PPR");

            cJSON_AddStringToObject(response, "status", "erro");
            cJSON_AddStringToObject(response, "message", s_motor_calib.message);
        } else {
            float final_ppr = sum_ppr / (float)used;
            esp_err_t err = rpm_counter_set_estimated_ppr(final_ppr, true);

            if (err != ESP_OK) {
                s_motor_calib.state = MOTOR_CALIB_ERROR;
                snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Falha ao salvar PPR no NVS");

                cJSON_AddStringToObject(response, "status", "erro");
                cJSON_AddStringToObject(response, "message", s_motor_calib.message);
            } else {
                s_motor_calib.state = MOTOR_CALIB_DONE;
                s_motor_calib.final_ppr = final_ppr;
                snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Calibração do motor concluída");

                cJSON_AddStringToObject(response, "status", "ok");
                cJSON_AddStringToObject(response, "message", s_motor_calib.message);
                cJSON_AddNumberToObject(response, "final_ppr", final_ppr);
            }
        }
    }

    cJSON_AddStringToObject(response, "state", motor_calib_state_str(s_motor_calib.state));

    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);

    return ESP_OK;
}

/**
 * @brief Handler para status da calibração do motor
 */
static esp_err_t handler_motor_calib_status(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/motor-calibration/status");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "state", motor_calib_state_str(s_motor_calib.state));
    cJSON_AddBoolToObject(response, "capturing", s_motor_calib.state == MOTOR_CALIB_CAPTURING);
    cJSON_AddNumberToObject(response, "current_point", s_motor_calib.current_point + 1);
    cJSON_AddNumberToObject(response, "total_points", MOTOR_CALIB_POINT_COUNT);
    cJSON_AddStringToObject(response, "message", s_motor_calib.message);
    cJSON_AddNumberToObject(response, "ppr", rpm_counter_get_estimated_ppr());
    cJSON_AddNumberToObject(response, "final_ppr", s_motor_calib.final_ppr);

    if (s_motor_calib.current_point < MOTOR_CALIB_POINT_COUNT) {
        cJSON_AddNumberToObject(response, "target_rpm", s_motor_calib_targets[s_motor_calib.current_point]);
    } else {
        cJSON_AddNumberToObject(response, "target_rpm", 0);
    }

    cJSON *measured_array = cJSON_CreateArray();
    for (int index = 0; index < MOTOR_CALIB_POINT_COUNT; index++) {
        cJSON_AddItemToArray(measured_array, cJSON_CreateNumber(s_motor_calib.measured_pps[index]));
    }
    cJSON_AddItemToObject(response, "measured_pps", measured_array);

    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    free(response_str);
    cJSON_Delete(response);

    return ESP_OK;
}

/**
 * @brief Handler para resetar sessão de calibração do motor
 */
static esp_err_t handler_motor_calib_reset(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/motor-calibration/reset");

    memset(&s_motor_calib, 0, sizeof(s_motor_calib));
    s_motor_calib.state = MOTOR_CALIB_IDLE;
    snprintf(s_motor_calib.message, sizeof(s_motor_calib.message), "Sessão de calibração do motor resetada");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "state", motor_calib_state_str(s_motor_calib.state));
    cJSON_AddStringToObject(response, "message", s_motor_calib.message);

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
    config.max_open_sockets = 6;
    config.max_uri_handlers = 20;
    
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

    // API Calibração Motor (RPM)
    httpd_uri_t uri_motor_calib_start = {
        .uri = "/api/motor-calibration/start",
        .method = HTTP_POST,
        .handler = handler_motor_calib_start,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_motor_calib_start);

    httpd_uri_t uri_motor_calib_capture = {
        .uri = "/api/motor-calibration/capture",
        .method = HTTP_POST,
        .handler = handler_motor_calib_capture,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_motor_calib_capture);

    httpd_uri_t uri_motor_calib_finish = {
        .uri = "/api/motor-calibration/finish",
        .method = HTTP_POST,
        .handler = handler_motor_calib_finish,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_motor_calib_finish);

    httpd_uri_t uri_motor_calib_status = {
        .uri = "/api/motor-calibration/status",
        .method = HTTP_GET,
        .handler = handler_motor_calib_status,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_motor_calib_status);

    httpd_uri_t uri_motor_calib_reset = {
        .uri = "/api/motor-calibration/reset",
        .method = HTTP_POST,
        .handler = handler_motor_calib_reset,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_motor_calib_reset);
    
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
