// pot_mqtt.c — AHT10 (I2C0) + buffer RAM + envio em blocos com sincronização pós-reconexão

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"

// FATFS
#include "fatfs.h"

#include "inc/ssd1306.h"
#include "aht10.h"

// === Módulos de rede ===
#include "wifi_conn.h"
#include "mqtt.h"

// ----------------- CONFIG Wi-Fi / MQTT -----------------
#define WIFI_SSID      "DarciRaul_2G"
#define WIFI_PASS      "40710413"
#define BROKER_IP      "200.137.1.176"
#define MQTT_USER      "desafio05"
#define MQTT_PASS      "desafio05.laica"
#define MQTT_CLIENT_ID "bitdoglab-pico-aht10"
#define TOPIC_POT      "ha/desafio05/micael.balza/pot"

// ----------------- Batch / Fila -----------------
#define QUEUE_CAP      256
#define BATCH_MAX      12
#define FLUSH_MS       8000
#define SAMPLE_MS      500

// ----------------- I2C -----------------
// AHT10 em I2C0 (BitDog: SDA0=GPIO0, SCL0=GPIO1)
#define AHT_I2C_INST   i2c0
#define AHT_I2C_SDA    0
#define AHT_I2C_SCL    1

// OLED em I2C1 (fisicamente nos GPIO14/15)
#define OLED_I2C_INST  i2c1
#define I2C_SDA_PIN    14
#define I2C_SCL_PIN    15
#define OLED_WIDTH     128
#define OLED_HEIGHT    32

// Separador decimal no OLED: vírgula
#define DEC_SEP ','

// ----------------- OLED helpers -----------------
static void oled_setup(void) {
    i2c_init(OLED_I2C_INST, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    ssd1306_init();
}

static void show_message_oled(char* message[], int lines) {
    struct render_area frame_area = {
        .start_column = 0,
        .end_column   = ssd1306_width - 1,
        .start_page   = 0,
        .end_page     = ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&frame_area);

    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    int y = 0;
    for (uint i = 0; i < (uint)lines; i++) {
        ssd1306_draw_string(ssd, 5, y, message[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
}

// ----------------- Fila circular de amostras -----------------
typedef struct {
    uint32_t ts_ms;   // timestamp desde boot (ms)
    float    temp_c;  // °C
    float    rh_pct;  // %UR
} Sample;

static Sample  qbuf[QUEUE_CAP];
static uint16_t q_head     = 0;   // índice do 1º válido
static uint16_t q_count    = 0;   // quantos válidos
static uint16_t pending_n  = 0;   // itens do lote aguardando PUBACK

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void q_push(const Sample *s) {
    if (q_count == QUEUE_CAP) { q_head = (q_head + 1) % QUEUE_CAP; q_count--; }
    uint16_t tail = (q_head + q_count) % QUEUE_CAP;
    qbuf[tail] = *s;
    q_count++;
}

static void q_pop_n(uint16_t n) {
    if (n > q_count) n = q_count;
    q_head = (q_head + n) % QUEUE_CAP;
    q_count -= n;
}

// Constrói JSON do próximo lote (sem remover da fila). Retorna len, ou -1 se faltou buffer.
static int build_batch_json(char *out, size_t outsz, uint16_t n) {
    size_t used = 0;
    int w = snprintf(out, outsz,
        "{\"dev\":\"bitdoglab\",\"count\":%u,\"batch\":[", (unsigned)n);
    if (w <= 0 || (size_t)w >= outsz) return -1;
    used += (size_t)w;

    uint16_t idx = q_head;
    for (uint16_t i = 0; i < n; i++) {
        const Sample *s = &qbuf[idx];
        w = snprintf(out + used, outsz - used,
                     "%s{\"t\":%u,\"tc\":%.2f,\"rh\":%.2f}",
                     (i ? "," : ""),
                     (unsigned)s->ts_ms, s->temp_c, s->rh_pct);
        if (w <= 0 || (size_t)w >= (outsz - used)) return -1;
        used += (size_t)w;
        idx = (idx + 1) % QUEUE_CAP;
    }

    w = snprintf(out + used, outsz - used, "]}");
    if (w <= 0 || (size_t)w >= (outsz - used)) return -1;
    used += (size_t)w;

    return (int)used;
}

// ----------------- Wi-Fi helpers -----------------
extern cyw43_t cyw43_state; // definido pela lib
static bool wifi_is_up(void) {
    int st = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    return st == CYW43_LINK_UP || st == CYW43_LINK_NOIP || st == CYW43_LINK_JOIN;
}

// Chamado quando PUBACK chega (registrado via mqtt_set_puback_callback)
static void on_puback(void) {
    if (pending_n) {
        uint16_t n = pending_n;
        q_pop_n(n);
        pending_n = 0;
        printf("[BUF] Removidos %u itens após PUBACK. Em fila: %u\n",
               (unsigned)n, (unsigned)q_count);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(500);

    // LED GREEN
    setup_ledg();
    // SPI
    setup_spi();
    // FATFS
    bool sd_mount = microsd_mount();
    bool sd_open = microsd_open();

    // OLED
    oled_setup();

    // AHT10 em I2C0 (GPIO0/1)
    if (!aht10_init(AHT_I2C_INST, AHT_I2C_SDA, AHT_I2C_SCL)) {
        printf("[AHT10] Falha ao inicializar (I2C0 SDA=%d, SCL=%d)\n",
               AHT_I2C_SDA, AHT_I2C_SCL);
    } else {
        printf("[AHT10] OK (I2C0)\n");
    }

    // Wi-Fi (primeira tentativa)
    if (connect_to_wifi(WIFI_SSID, WIFI_PASS) == 0) {
        printf("[WiFi] Conectado em '%s'\n", WIFI_SSID);
    } else {
        printf("[WiFi] ERRO ao conectar em '%s'\n", WIFI_SSID);
    }

    // MQTT
    mqtt_set_puback_callback(on_puback);
    char status_buf[100] = {0};
    if (mqtt_setup(MQTT_CLIENT_ID, BROKER_IP, MQTT_USER, MQTT_PASS, status_buf) != 0) {
        printf("[MQTT] Falha ao iniciar cliente: %s\n", status_buf);
    } else {
        printf("[MQTT] Cliente iniciado: %s\n", status_buf[0] ? status_buf : "OK\n");
    }

    // buffers display
    char s_t[20]     = "T: 0,00C";
    char s_rh[20]    = "RH: 0,0%";
    char s_q[20]     = "Q: 0";
    char s_state[20] = "MQTT: ?";

    // timers
    absolute_time_t next_sample = make_timeout_time_ms(SAMPLE_MS);
    absolute_time_t next_flush  = make_timeout_time_ms(FLUSH_MS);

    static char payload[1024];

    // backoff reconexão Wi-Fi e MQTT
    static uint32_t wifi_backoff_ms = 5000, mqtt_backoff_ms = 5000;
    static absolute_time_t next_wifi_retry = {0}, next_mqtt_retry = {0};

    while (true) {
        // -------- Coleta AHT10 + OLED (formatação manual com separador vírgula) --------
        if (absolute_time_diff_us(get_absolute_time(), next_sample) <= 0) {
            next_sample = make_timeout_time_ms(SAMPLE_MS);

            float tc = 0.0f, rh = 0.0f;
            if (aht10_read(AHT_I2C_INST, &tc, &rh)) {
                Sample s = { .ts_ms = now_ms(), .temp_c = tc, .rh_pct = rh };
                q_push(&s);

                // T: xx,xxC (2 casas)
                int t100 = (int)((tc >= 0 ? tc*100.0f + 0.5f : tc*100.0f - 0.5f));
                int t_int = t100 / 100;
                int t_dec = abs(t100 % 100);
                snprintf(s_t, sizeof(s_t), "T: %d%c%02dC", t_int, DEC_SEP, t_dec);

                // RH: yy,y% (1 casa)
                int rh10 = (int)(rh*10.0f + 0.5f);
                snprintf(s_rh, sizeof(s_rh), "RH: %d%c%01d%%", rh10/10, DEC_SEP, abs(rh10%10));
            } else {
                snprintf(s_t,  sizeof(s_t),  "T: ----");
                snprintf(s_rh, sizeof(s_rh), "RH: ----");
            }

            snprintf(s_q,     sizeof(s_q),     "Q: %u", (unsigned)q_count);
            snprintf(s_state, sizeof(s_state), "MQTT: %s", mqtt_is_connected() ? "OK" : "OFF");

            char *showing_text[] = { s_t, s_rh, s_q, s_state };
            show_message_oled(showing_text, 4);
        }

        // -------- Reconexão Wi-Fi (backoff) --------
        if (!wifi_is_up()) {
            if (absolute_time_diff_us(get_absolute_time(), next_wifi_retry) <= 0) {
                int rc = cyw43_arch_wifi_connect_timeout_ms(
                    WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 8000);
                printf("[WiFi] retry rc=%d\n", rc);
                next_wifi_retry = make_timeout_time_ms(wifi_backoff_ms);
                if (wifi_backoff_ms < 60000) wifi_backoff_ms <<= 1;
            }
        } else {
            wifi_backoff_ms = 5000; // reset quando voltar
        }

        // -------- Reconexão MQTT (backoff) --------
        if (!mqtt_is_connected() && wifi_is_up()) {
            if (absolute_time_diff_us(get_absolute_time(), next_mqtt_retry) <= 0) {
                mqtt_setup(MQTT_CLIENT_ID, BROKER_IP, MQTT_USER, MQTT_PASS, status_buf);
                printf("[MQTT] Reconectando... %s\n", status_buf);
                next_mqtt_retry = make_timeout_time_ms(mqtt_backoff_ms);
                if (mqtt_backoff_ms < 60000) mqtt_backoff_ms <<= 1;
            }
        } else if (mqtt_is_connected()) {
            mqtt_backoff_ms = 5000;
        }

        // -------- Envio do próximo lote --------
        uint16_t n;
        int len = 0;
        bool time_to_flush = (absolute_time_diff_us(get_absolute_time(), next_flush) <= 0);
        if ((q_count >= BATCH_MAX) || (q_count > 0 && time_to_flush)) {
            n = (q_count < BATCH_MAX) ? q_count : BATCH_MAX;
            len = build_batch_json(payload, sizeof(payload), n);
            if (sd_mount && sd_open) {
                microsd_write(payload);
            }
        }
        if (mqtt_is_connected() && !mqtt_publish_inflight()) {
                if (len > 0) {
                    err_t st = mqtt_comm_publish(TOPIC_POT, (const uint8_t*)payload, (size_t)len);
                    if (st == ERR_OK) {
                        pending_n = n;
                        next_flush = make_timeout_time_ms(FLUSH_MS);
                        printf("[MQTT] Lote enviado (%u itens). Em fila: %u\n",
                               (unsigned)pending_n, (unsigned)q_count);
                    } else if (st != ERR_INPROGRESS) {
                        printf("[MQTT] Publish ERR=%d (lote não removido)\n", st);
                    }
                } else {
                    if (n > 1) next_flush = make_timeout_time_ms(500);
                }
            }

        sleep_ms(10);
    }
}
