// pot_mqtt.c — AHT10 (I2C0) + buffer RAM + envio em blocos com sincronização pós-reconexão

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"

#include "hardware/gpio.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

// FATFS
#include "fatfs.h"

#include "inc/ssd1306.h"
#include "aht10.h"

// === Módulos de rede ===
#include "wifi_conn.h"
#include "mqtt.h"

// ----------------- CONFIG Wi-Fi / MQTT -----------------
#define WIFI_SSID      "NOME_DA_REDE_WIFI"
#define WIFI_PASS      "PASSWORD_DA_REDE_WIFI"
#define BROKER_IP      "200.137.1.176"
#define MQTT_USER      "desafio05"
#define MQTT_PASS      "desafio05.laica"
#define MQTT_CLIENT_ID "bitdoglab-pico-aht10"
#define TOPIC_POT      "ha/desafio05/micael.balza/pot"

// ----------------- Batch / Fila -----------------
#define QUEUE_CAP      256
#define BATCH_MAX      12
#define FLUSH_MS       8000
#define SAMPLE_MS      1000

// ----------------- I2C -----------------
// AHT10 em I2C0 (BitDog: SDA0=GPIO0, SCL0=GPIO1)
#define AHT_I2C_INST   i2c1
#define AHT_I2C_SDA    2
#define AHT_I2C_SCL    3

// OLED em I2C1 (fisicamente nos GPIO14/15)
#define OLED_I2C_INST  i2c1
#define I2C_SDA_PIN    14
#define I2C_SCL_PIN    15
#define OLED_WIDTH     128
#define OLED_HEIGHT    32

// TODO: passar todo esse codigo do ds3231 para uma lib e organizar o codigo
// Configurações I2C DS3231
#define I2C_PORT_DS3231 i2c0
#define I2C_SDA_PIN_DS3231 0
#define I2C_SCL_PIN_DS3231 1
// #define I2C_PORT_DS3231 i2c1
// #define I2C_SDA_PIN_DS3231 2
// #define I2C_SCL_PIN_DS3231 3
#define I2C_BAUDRATE_DS3231 100000

// Registradores do DS3231
#define DS3231_I2C_ADDRESS 0x68
#define DS3231_REG_SECONDS 0x00
#define DS3231_REG_MINUTES 0x01
#define DS3231_REG_HOURS   0x02
#define DS3231_REG_DAY     0x03
#define DS3231_REG_DATE    0x04
#define DS3231_REG_MONTH   0x05
#define DS3231_REG_YEAR    0x06
#define DS3231_REG_CONTROL 0x0E

// Configurações cliente NTP 
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // Diferença entre 1900 e 1970
#define NTP_TIMEOUT (30 * 1000)
#define NTP_SERVER "pool.ntp.org" // servidor ntp
#define NTP_DELTA 2208988800 // diferenca de segundos desde comeco do seculo XX ate o unix time 1 Jan 1900 and 1 Jan 1970
#define NTP_TEST_TIME_MS (60 * 1000) // uma requisição por minuto para o servidor NTP
#define NTP_RESEND_TIME_MS (10 * 1000)
// Configuração de Timezone
#define TIMEZONE_OFFSET_HOURS (-3)  // UTC-3 (Brasília)
#define TIMEZONE_OFFSET_SECONDS (TIMEZONE_OFFSET_HOURS * 3600)

// Struct para variavel de estado da sincronização NTP
typedef struct NTP_T_ {
    ip_addr_t ntp_server_address;
    struct udp_pcb *ntp_pcb;
    async_at_time_worker_t request_worker;
    async_at_time_worker_t resend_worker;
} NTP_T;

// Struct para armazenar data/hora no modulo DS3231
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t date;
    uint8_t month;
    uint8_t year;
} rtc_datetime_t;


// *** COMEÇO trecho do codigo para uso do ds3231
// Função para converter BCD para decimal
uint8_t bcd_to_decimal(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Função para converter decimal para BCD
uint8_t decimal_to_bcd(uint8_t decimal) {
    return ((decimal / 10) << 4) | (decimal % 10);
}

// Inicializar I2C
void init_i2c_ds3231() {
    i2c_init(I2C_PORT_DS3231, I2C_BAUDRATE_DS3231);
    gpio_set_function(I2C_SDA_PIN_DS3231, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN_DS3231, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN_DS3231);
    gpio_pull_up(I2C_SCL_PIN_DS3231);
    
    printf("I2C DS3231 inicializado - SDA: GPIO%d, SCL: GPIO%d\n", I2C_SDA_PIN_DS3231, I2C_SCL_PIN_DS3231);
}

// Verificar se DS3231 está conectado
bool ds3231_is_connected() {
    uint8_t data;
    int result = i2c_read_blocking(I2C_PORT_DS3231, DS3231_I2C_ADDRESS, &data, 1, false);
    return result == 1;
}

// Ler registrador do DS3231
uint8_t ds3231_read_register(uint8_t reg) {
    uint8_t data;
    // w_err e r_err para futuro debug;
    int w_err = i2c_write_blocking(I2C_PORT_DS3231, DS3231_I2C_ADDRESS, &reg, 1, true);
    int r_err = i2c_read_blocking(I2C_PORT_DS3231, DS3231_I2C_ADDRESS, &data, 1, false);

    return data;
}

// Escrever registrador do DS3231
void ds3231_write_register(uint8_t reg, uint8_t data) {
    uint8_t buffer[2] = {reg, data};
    i2c_write_blocking(I2C_PORT_DS3231, DS3231_I2C_ADDRESS, buffer, 2, false);
}

// Inicializar DS3231
bool ds3231_init() {
    if (!ds3231_is_connected()) {
        printf("ERRO: DS3231 não encontrado no endereço 0x%02X\n", DS3231_I2C_ADDRESS);
        return false;
    }
    
    printf("DS3231 conectado com sucesso!\n");    
    // Configurar registrador de controle (desabilitar alarmes, habilitar oscilador)
    // ds3231_write_register(DS3231_REG_CONTROL, 0x00);
    
    return true;
}

// Configurar data/hora no DS3231 (apenas se necessário)
void ds3231_set_datetime(rtc_datetime_t *datetime) {
    ds3231_write_register(DS3231_REG_SECONDS, decimal_to_bcd(datetime->seconds));
    ds3231_write_register(DS3231_REG_MINUTES, decimal_to_bcd(datetime->minutes));
    ds3231_write_register(DS3231_REG_HOURS, decimal_to_bcd(datetime->hours));
    ds3231_write_register(DS3231_REG_DAY, decimal_to_bcd(datetime->day));
    ds3231_write_register(DS3231_REG_DATE, decimal_to_bcd(datetime->date));
    ds3231_write_register(DS3231_REG_MONTH, decimal_to_bcd(datetime->month));
    ds3231_write_register(DS3231_REG_YEAR, decimal_to_bcd(datetime->year));
    
    printf("Data/hora configurada: %02d/%02d/20%02d %02d:%02d:%02d\n",
           datetime->date, datetime->month, datetime->year,
           datetime->hours, datetime->minutes, datetime->seconds);
}

// Ler data/hora do DS3231
void ds3231_get_datetime(rtc_datetime_t *datetime) {
    datetime->seconds = bcd_to_decimal(ds3231_read_register(DS3231_REG_SECONDS) & 0x7F);
    datetime->minutes = bcd_to_decimal(ds3231_read_register(DS3231_REG_MINUTES));
    datetime->hours = bcd_to_decimal(ds3231_read_register(DS3231_REG_HOURS) & 0x3F);
    datetime->day = bcd_to_decimal(ds3231_read_register(DS3231_REG_DAY));
    datetime->date = bcd_to_decimal(ds3231_read_register(DS3231_REG_DATE));
    datetime->month = bcd_to_decimal(ds3231_read_register(DS3231_REG_MONTH) & 0x7F);
    datetime->year = bcd_to_decimal(ds3231_read_register(DS3231_REG_YEAR));
}

// Gerar timestamp formatado
void generate_timestamp(char *buffer, size_t buffer_size) {
    rtc_datetime_t current_time;
    ds3231_get_datetime(&current_time);
    
    // Formato: YYYY-MM-DD HH:MM:SS
    snprintf(buffer, buffer_size, "%02d-%02d-%02d %02d:%02d:%02d",
             current_time.date, current_time.month,  current_time.year,
             current_time.hours, current_time.minutes, current_time.seconds);
}
// *** FIM do trecho do codigo para uso do ds3231

// *** COMEÇO do trecho do codigo para uso do cliente ntp
// Função com resultado da request e agendando uma nova execução futura
static void ntp_result(NTP_T* state, int status, time_t *result) {
    if (status == 0 && result) {
        // struct tm *utc = gmtime(result);

        time_t local_time = *result + TIMEZONE_OFFSET_SECONDS;
        struct tm *local_tm = gmtime(&local_time);
        
        printf("NTP sincronizado: %02d/%02d/%04d %02d:%02d:%02d (UTC%+d)\n", 
               local_tm->tm_mday, local_tm->tm_mon + 1, local_tm->tm_year + 1900,
               local_tm->tm_hour, local_tm->tm_min, local_tm->tm_sec, TIMEZONE_OFFSET_HOURS);


        rtc_datetime_t initial_time = {
            .seconds = local_tm->tm_sec,
            .minutes = local_tm->tm_min,
            .hours = local_tm->tm_hour,
            .day = local_tm->tm_wday,      // 1=domingo, 2=segunda, etc.
            .date = local_tm->tm_mday,     // dia do mês
            .month = local_tm->tm_mon + 1,    // setembro
            .year = local_tm->tm_year % 100     // 2025 (apenas os últimos 2 dígitos)
        };
        ds3231_set_datetime(&initial_time);
    }
    async_context_remove_at_time_worker(cyw43_arch_async_context(), &state->resend_worker);
    hard_assert(async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(),  &state->request_worker, NTP_TEST_TIME_MS)); // repeat the request in future
    // printf("Next request in %ds\n", NTP_TEST_TIME_MS / 1000);
}

// Make an NTP request
static void ntp_request(NTP_T *state) {
    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *req = (uint8_t *) p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1b;
    udp_sendto(state->ntp_pcb, p, &state->ntp_server_address, NTP_PORT);
    pbuf_free(p);
    cyw43_arch_lwip_end();
}

// Call back with a DNS result
static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    NTP_T *state = (NTP_T*)arg;
    if (ipaddr) {
        state->ntp_server_address = *ipaddr;
        printf("ntp address %s\n", ipaddr_ntoa(ipaddr));
        ntp_request(state);
    } else {
        printf("ntp dns request failed\n");
        ntp_result(state, -1, NULL);
    }
}

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    NTP_T *state = (NTP_T*)arg;
    uint8_t mode = pbuf_get_at(p, 0) & 0x7;
    uint8_t stratum = pbuf_get_at(p, 1);

    // Check the result
    if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
        mode == 0x4 && stratum != 0) {
        uint8_t seconds_buf[4] = {0};
        pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
        uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
        uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
        time_t epoch = seconds_since_1970;
        ntp_result(state, 0, &epoch);
    } else {
        printf("invalid ntp response\n");
        ntp_result(state, -1, NULL);
    }
    pbuf_free(p);
}

// Called to make a NTP request
static void request_worker_fn(__unused async_context_t *context, async_at_time_worker_t *worker) {
    NTP_T* state = (NTP_T*)worker->user_data;
    hard_assert(async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &state->resend_worker, NTP_RESEND_TIME_MS)); // in case UDP request is lost
    int err = dns_gethostbyname(NTP_SERVER, &state->ntp_server_address, ntp_dns_found, state);
    if (err == ERR_OK) {
        ntp_request(state); // Cached DNS result, make NTP request
    } else if (err != ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
        printf("dns request failed\n");
        ntp_result(state, -1, NULL);
    }
}

// Called to resend an NTP request if it appears to get lost
static void resend_worker_fn(__unused async_context_t *context, async_at_time_worker_t *worker) {
    NTP_T* state = (NTP_T*)worker->user_data;
    printf("ntp request failed\n");
    ntp_result(state, -1, NULL);
}

// Perform initialisation
static NTP_T* ntp_init(void) {
    NTP_T *state = (NTP_T*)calloc(1, sizeof(NTP_T));
    if (!state) {
        printf("Falhou em alocar estado NTP\n");
        return NULL;
    }
    state->ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!state->ntp_pcb) {
        printf("Falhou em criar PCB para NTP \n");
        free(state);
        return NULL;
    }
    udp_recv(state->ntp_pcb, ntp_recv, state);
    state->request_worker.do_work = request_worker_fn;
    state->request_worker.user_data = state;
    state->resend_worker.do_work = resend_worker_fn;
    state->resend_worker.user_data = state;
    return state;
}

// Runs ntp test forever
void init_ntp_client(void) {
    NTP_T *state = ntp_init();
    if (!state){
        printf("\nFalhou em inicializar cliente NTP!\n");
        return;
    }
    printf("\nCriou cliente NTP!\n");        
    hard_assert(async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(),  &state->request_worker, 0)); // make the first request
    // free(state);
}

// *** FIM do trecho do codigo para uso do cliente ntp



// Separador decimal no OLED: vírgula
#define DEC_SEP ','

// ----------------- OLED helpers -----------------
// static void oled_setup(void) {
//     i2c_init(OLED_I2C_INST, ssd1306_i2c_clock * 1000);
//     gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA_PIN);
//     gpio_pull_up(I2C_SCL_PIN);
//     ssd1306_init();
// }

// static void show_message_oled(char* message[], int lines) {
//     struct render_area frame_area = {
//         .start_column = 0,
//         .end_column   = ssd1306_width - 1,
//         .start_page   = 0,
//         .end_page     = ssd1306_n_pages - 1
//     };
//     calculate_render_area_buffer_length(&frame_area);

//     uint8_t ssd[ssd1306_buffer_length];
//     memset(ssd, 0, ssd1306_buffer_length);
//     render_on_display(ssd, &frame_area);

//     int y = 0;
//     for (uint i = 0; i < (uint)lines; i++) {
//         ssd1306_draw_string(ssd, 5, y, message[i]);
//         y += 8;
//     }
//     render_on_display(ssd, &frame_area);
// }

// ----------------- Fila circular de amostras -----------------
typedef struct {
    char ts_ms[32];   // timestamp desde boot (ms)
    float    temp_c;  // °C
    float    rh_pct;  // %UR
} Sample;

static Sample  qbuf[QUEUE_CAP];
static uint16_t q_head     = 0;   // índice do 1º válido
static uint16_t q_count    = 0;   // quantos válidos
static uint16_t pending_n  = 0;   // itens do lote aguardando PUBACK

static inline uint32_t now_ms(void) { 
    return to_ms_since_boot(get_absolute_time()); 

}

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
                     "%s{\"t\":%s,\"tc\":%.2f,\"rh\":%.2f}",
                     (i ? "," : ""),
                     s->ts_ms, s->temp_c, s->rh_pct);
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

// // função que eu tava utilizando para verificar os dispositivos i2c conectados, não é mais necessária
// void scan_i2c_devices() {
//     printf("Dispositivos no barramento I2C1:\n");
    
//     for (int addr = 0x08; addr < 0x78; addr++) {
//         uint8_t data;
//         int ret = i2c_read_blocking(i2c1, addr, &data, 1, false);
        
//         if (ret >= 0) {
//             printf("Encontrado: 0x%02X", addr);
            
//             // Identificar dispositivo
//             if (addr == 0x68) printf(" (DS3231 RTC)");
//             else if (addr == 0x3C || addr == 0x3D) printf(" (OLED SSD1306/SH1106)");
//             else if (addr == 0x57) printf(" (EEPROM AT24C32)");            
//             printf("\n");
//         }
//     }
// }

int main(void) {
    stdio_init_all();
    
    sleep_ms(10000);
    printf("\nstart\n");

    // LED GREEN
    setup_ledg();
    // SPI
    setup_spi();
    printf("\nled e spi ok\n");
    // FATFS
    bool sd_mount = microsd_mount();
    bool sd_open = microsd_open();

    // OLED
    // oled_setup();
    // char *showing_text[] = { "starting", "oled" };
    // show_message_oled(showing_text, 2);
    // printf("\noled ok\n");

    // AHT10 em I2C0 (GPIO0/1)
    if (!aht10_init(AHT_I2C_INST, AHT_I2C_SDA, AHT_I2C_SCL)) {
        printf("[AHT10] Falha ao inicializar (I2C1 SDA=%d, SCL=%d)\n",
               AHT_I2C_SDA, AHT_I2C_SCL);
    } else {
        printf("[AHT10] OK (I2C1)\n");
    }

    // Wi-Fi (primeira tentativa)
    if (connect_to_wifi(WIFI_SSID, WIFI_PASS) == 0) {
        printf("[WiFi] Conectado em '%s'\n", WIFI_SSID);
    } else {
        printf("[WiFi] ERRO ao conectar em '%s'\n", WIFI_SSID);
    }


    // cliente ntp
    init_ntp_client();
    printf("\nntp ok\n");
    // inicializacao ds3231
    init_i2c_ds3231();
    sleep_ms(1000);    
    // Inicializar DS3231
    if (!ds3231_init()) {
        printf("Falha ao inicializar DS3231. Verifique as conexões.\n");
        // return -1;
    }
    printf("\nds3231 ok\n");

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

        if (!sd_mount && !sd_open) {
            sd_mount = microsd_mount();
            sd_open = microsd_open();
            sleep_ms(3000);
        }
        // -------- Coleta AHT10 + OLED (formatação manual com separador vírgula) --------
        if (absolute_time_diff_us(get_absolute_time(), next_sample) <= 0) {
            next_sample = make_timeout_time_ms(SAMPLE_MS);

            float tc = 0.0f, rh = 0.0f;
            if (aht10_read(AHT_I2C_INST, &tc, &rh)) {
                char timestamp[31] = "999999999";
                generate_timestamp(timestamp, sizeof(timestamp));
                // printf("\ntempo gerado em timestamp: %s", timestamp);
                Sample s;
                // = { .ts_ms = timestamp, .temp_c = tc, .rh_pct = rh }
                strcpy(s.ts_ms, timestamp);
                s.temp_c = tc;
                s.rh_pct = rh;
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

            // char *showing_text[] = { s_t, s_rh, s_q, s_state };
            // show_message_oled(showing_text, 4);
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
                strcat(payload, "\r\n");
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
