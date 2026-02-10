#pragma once
// mqtt_send_module.h
// MQTT Send Module: buffers telemetry samples and sends them in batches via MQTT (ESP-IDF + FreeRTOS)
//
// IMPORTANTE (TIPOS COMPARTILHADOS):
// - O tipo jh_state_t (estado do joystick) é definido em joystick_hall.h.
// - NÃO definimos um enum local aqui para evitar conflito de símbolos (redeclaration).
// - Portanto, este módulo "usa" o jh_state_t vindo do joystick_hall.h.

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"   // UBaseType_t, BaseType_t
#include "mqtt_client.h"         // esp_mqtt_client_handle_t

// Tipo jh_state_t vem daqui:
#include "joystick_hall.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t     ts_ms;         // timestamp in milliseconds
    uint8_t     front_percent; // 0..100
    uint8_t     back_percent;  // 0..100
    jh_state_t  state;         // MID/FRONT/BACK/NOT_CALIBRATED (do joystick_hall.h)
    float       rpm;           // RPM value
} mqtt_send_sample_t;

typedef struct {
    const char *topic;

    // Queue capacity (items). Example: 256
    uint16_t capacity_items;

    // Target batch size (items). Example: 20
    uint16_t batch_items;

    // Max MQTT payload size (bytes). Example: 2048, 4096...
    size_t payload_bytes_max;

    // Flush period (ms): send even if batch not full. Example: 10000
    uint32_t flush_period_ms;

    // QoS/retain
    int qos;     // 0,1,2
    int retain;  // 0/1

    // On full queue:
    // true  -> drop oldest (overwrite oldest)
    // false -> drop newest (reject new sample)
    bool drop_oldest_on_full;

    // Payload format:
    // true  -> JSON array: [{"ts":..,"front":..,"back":..,"state":"MID","rpm":..}, ...]
    // false -> JSON Lines: one JSON object per line
    bool json_array_mode;

    // Task configuration
    const char *task_name;
    uint32_t task_stack_words;   // e.g. 4096
    UBaseType_t task_priority;   // e.g. 5
    BaseType_t task_core;        // -1 no pin; 0/1 pinned core
} mqtt_send_module_config_t;

typedef struct mqtt_send_module_ctx mqtt_send_module_ctx_t;

// Create + start module
mqtt_send_module_ctx_t *mqtt_send_module_init(
    const mqtt_send_module_config_t *cfg,
    esp_mqtt_client_handle_t mqtt_client
);

// Notify module about MQTT connection state (call from your MQTT event handler)
void mqtt_send_module_set_connected(mqtt_send_module_ctx_t *ctx, bool connected);

// Must be called when MQTT_EVENT_PUBLISHED arrives (ACK for the msg_id)
void mqtt_send_module_on_published(mqtt_send_module_ctx_t *ctx, int msg_id);

// Push one telemetry sample (thread-safe)
bool mqtt_send_module_push_sample(mqtt_send_module_ctx_t *ctx,
                                 const mqtt_send_sample_t *s);

// Request flush (non-blocking)
void mqtt_send_module_request_flush(mqtt_send_module_ctx_t *ctx);

// State
uint16_t mqtt_send_module_count(mqtt_send_module_ctx_t *ctx);
bool     mqtt_send_module_is_connected(mqtt_send_module_ctx_t *ctx);

// Deinit
void mqtt_send_module_deinit(mqtt_send_module_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
