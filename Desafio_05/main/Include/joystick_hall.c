#include "joystick_hall.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "esp_log.h"
#include "nvs.h"        // ATENÇÃO: o NVS precisa ter sido inicializado no app_main()

#define TAG "JOY_HALL"

// NVS
#define JH_NVS_NAMESPACE   "joy_hall"
#define JH_NVS_KEY_CAL     "cal_v1"

typedef struct {
    uint32_t magic;     // validação simples
    uint16_t version;
    uint16_t reserved;

    // Calibração em ADC raw (média)
    int32_t a_mid, b_mid, d_mid;
    int32_t a_front, b_front, d_front;
    int32_t a_back, b_back, d_back;

    // Flags
    uint8_t has_mid;
    uint8_t has_front;
    uint8_t has_back;
    uint8_t pad;
} jh_cal_t;

static joystick_hall_config_t s_cfg;
static jh_cal_t s_cal;
static bool s_cal_loaded = false;

/**
 * @brief Carrega calibração do NVS.
 *
 * @note Este módulo NÃO inicializa NVS. O app_main deve chamar nvs_flash_init() no início.
 */
static esp_err_t jh_load_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(JH_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t sz = sizeof(jh_cal_t);
    jh_cal_t tmp;
    err = nvs_get_blob(h, JH_NVS_KEY_CAL, &tmp, &sz);
    nvs_close(h);

    if (err == ESP_OK && sz == sizeof(jh_cal_t) && tmp.magic == 0x4A48434C && tmp.version == 1) {
        s_cal = tmp;
        s_cal_loaded = (s_cal.has_mid && s_cal.has_front && s_cal.has_back);
        return ESP_OK;
    }
    return ESP_FAIL;
}

/**
 * @brief Salva calibração atual no NVS.
 *
 * @note Este módulo NÃO inicializa NVS. O app_main deve chamar nvs_flash_init() no início.
 */
static esp_err_t jh_save_to_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(JH_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(h, JH_NVS_KEY_CAL, &s_cal, sizeof(jh_cal_t));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static int read_adc_avg(adc1_channel_t ch, int n)
{
    int32_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum += adc1_get_raw(ch);
    }
    return (int)(sum / n);
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

esp_err_t joystick_hall_init(const joystick_hall_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (cfg->samples_per_read <= 0) return ESP_ERR_INVALID_ARG;

    s_cfg = *cfg;

    // ------------------------------------------------------------
    // (1) Configuração do ADC
    // ------------------------------------------------------------
    adc1_config_width(s_cfg.width);
    adc1_config_channel_atten(s_cfg.sensor_a_ch, s_cfg.atten);
    adc1_config_channel_atten(s_cfg.sensor_b_ch, s_cfg.atten);

    // ------------------------------------------------------------
    // (2) Prepara estrutura de calibração em RAM
    // ------------------------------------------------------------
    memset(&s_cal, 0, sizeof(s_cal));
    s_cal.magic = 0x4A48434C; // 'JHCL'
    s_cal.version = 1;

    // ------------------------------------------------------------
    // (3) Tenta carregar calibração do NVS
    //     IMPORTANTE: NVS precisa estar inicializado no app_main
    // ------------------------------------------------------------
    esp_err_t err = jh_load_from_nvs();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibracao carregada do NVS.");
    } else {
        s_cal_loaded = false;
        ESP_LOGW(TAG, "Sem calibracao no NVS (precisa calibrar). (err=%s)", esp_err_to_name(err));
        // Não retorna erro aqui: o sistema pode operar e pedir calibração pela main.
    }

    return ESP_OK;
}

bool joystick_hall_is_calibrated(void)
{
    return s_cal_loaded;
}

esp_err_t joystick_hall_clear_calibration(void)
{
    memset(&s_cal, 0, sizeof(s_cal));
    s_cal.magic = 0x4A48434C;
    s_cal.version = 1;
    s_cal_loaded = false;

    // Remove do NVS (se existir)
    nvs_handle_t h;
    esp_err_t err = nvs_open(JH_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(h, JH_NVS_KEY_CAL);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t joystick_hall_read_raw(int *out_a, int *out_b, int *out_d)
{
    if (!out_a || !out_b || !out_d) return ESP_ERR_INVALID_ARG;

    int a = read_adc_avg(s_cfg.sensor_a_ch, s_cfg.samples_per_read);
    int b = read_adc_avg(s_cfg.sensor_b_ch, s_cfg.samples_per_read);
    int d = a - b;

    *out_a = a;
    *out_b = b;
    *out_d = d;
    return ESP_OK;
}

esp_err_t joystick_hall_capture_point(jh_cal_point_t point)
{
    int a, b, d;
    esp_err_t err = joystick_hall_read_raw(&a, &b, &d);
    if (err != ESP_OK) return err;

    switch (point) {
        case JH_POS_MID:
            s_cal.a_mid = a; s_cal.b_mid = b; s_cal.d_mid = d;
            s_cal.has_mid = 1;
            ESP_LOGI(TAG, "CAPTURE MID: A=%d B=%d D=%d", a, b, d);
            break;

        case JH_POS_FRONT:
            s_cal.a_front = a; s_cal.b_front = b; s_cal.d_front = d;
            s_cal.has_front = 1;
            ESP_LOGI(TAG, "CAPTURE FRONT: A=%d B=%d D=%d", a, b, d);
            break;

        case JH_POS_BACK:
            s_cal.a_back = a; s_cal.b_back = b; s_cal.d_back = d;
            s_cal.has_back = 1;
            ESP_LOGI(TAG, "CAPTURE BACK: A=%d B=%d D=%d", a, b, d);
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t joystick_hall_save_calibration(void)
{
    if (!(s_cal.has_mid && s_cal.has_front && s_cal.has_back)) {
        ESP_LOGE(TAG, "Faltam pontos: mid=%d front=%d back=%d",
                 s_cal.has_mid, s_cal.has_front, s_cal.has_back);
        return ESP_ERR_INVALID_STATE;
    }

    // sanity: spans não podem ser ~0
    int span_front = s_cal.d_front - s_cal.d_mid;
    int span_back  = s_cal.d_mid - s_cal.d_back;

    if (span_front == 0 || span_back == 0) {
        ESP_LOGE(TAG, "Span zero. Refaça a calibração. (Dmid=%ld Df=%ld Db=%ld)",
                 (long)s_cal.d_mid, (long)s_cal.d_front, (long)s_cal.d_back);
        return ESP_FAIL;
    }

    esp_err_t err = jh_save_to_nvs();
    if (err == ESP_OK) {
        s_cal_loaded = true;
        ESP_LOGI(TAG, "Calibracao salva no NVS.");
    }
    return err;
}

esp_err_t joystick_hall_read(joystick_hall_reading_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    int a, b, d;
    esp_err_t err = joystick_hall_read_raw(&a, &b, &d);
    if (err != ESP_OK) return err;

    memset(out, 0, sizeof(*out));
    out->a_raw = a;
    out->b_raw = b;
    out->d_raw = d;

    if (!s_cal_loaded) {
        out->state = JH_STATE_NOT_CALIBRATED;
        return ESP_OK;
    }

    const int d_mid   = (int)s_cal.d_mid;
    const int d_front = (int)s_cal.d_front;
    const int d_back  = (int)s_cal.d_back;

    // spans (podem ser negativos dependendo de como ficou, mas o diferencial resolve com comparação ao mid)
    float front_span = (float)(d_front - d_mid);
    float back_span  = (float)(d_mid - d_back);

    // deadband
    int db = clamp_i(s_cfg.deadband_percent, 0, 50);
    float dead_front = fabsf(front_span) * (db / 100.0f);
    float dead_back  = fabsf(back_span)  * (db / 100.0f);

    if (d >= d_mid) {
        // lado "frente" (por definição: acima do mid)
        float num = (float)(d - d_mid);
        float den = front_span;
        float pct = 0.0f;

        if (den != 0.0f) pct = (num / den) * 100.0f;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;

        // deadband ao redor do meio
        if (num <= dead_front) pct = 0.0f;

        out->front_percent = (int)(pct + 0.5f);
        out->back_percent  = 0;

        out->state = (out->front_percent == 0) ? JH_STATE_MID : JH_STATE_FRONT;
    } else {
        // lado "tras" (abaixo do mid)
        float num = (float)(d_mid - d);
        float den = back_span;
        float pct = 0.0f;

        if (den != 0.0f) pct = (num / den) * 100.0f;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;

        if (num <= dead_back) pct = 0.0f;

        out->front_percent = 0;
        out->back_percent  = (int)(pct + 0.5f);

        out->state = (out->back_percent == 0) ? JH_STATE_MID : JH_STATE_BACK;
    }

    return ESP_OK;
}
