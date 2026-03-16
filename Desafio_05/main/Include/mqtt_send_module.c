// mqtt_send_module.c
#include "mqtt_send_module.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "MQTT_SEND_MODULE"

// Event bits
#define EVT_HAVE_DATA     (1 << 0)
#define EVT_FORCE_FLUSH   (1 << 1)
#define EVT_CONNECTED     (1 << 2)
#define EVT_PUBLISHED_OK  (1 << 3)

struct mqtt_send_module_ctx {
    mqtt_send_module_config_t cfg;
    esp_mqtt_client_handle_t client;

    // Ring buffer of samples (fixed-size, preserves order)
    mqtt_send_sample_t *buf;
    uint16_t head;   // oldest item index
    uint16_t tail;   // next write index
    uint16_t count;  // items currently stored

    SemaphoreHandle_t mtx;

    EventGroupHandle_t ev;
    TaskHandle_t task;

    volatile bool connected;

    // Publish/ACK
    SemaphoreHandle_t pub_mtx;       // ensures only one publish at a time
    volatile int awaiting_msg_id;    // msg_id waiting for ACK
};

static uint16_t _min_u16(uint16_t a, uint16_t b) { return (a < b) ? a : b; }

static const char* _state_to_str(jh_state_t s) {
    switch (s) {
        case JH_STATE_MID:            return "MID";
        case JH_STATE_FRONT:          return "FRONT";
        case JH_STATE_BACK:           return "BACK";
        case JH_STATE_NOT_CALIBRATED: return "NOT_CALIBRATED";
        default:                      return "UNKNOWN";
    }
}

static bool _publish_and_wait_ack(mqtt_send_module_ctx_t *ctx, const char *payload, int payload_len) {
    if (!ctx->connected) return false;

    if (xSemaphoreTake(ctx->pub_mtx, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return false;
    }

    ctx->awaiting_msg_id = -1;
    xEventGroupClearBits(ctx->ev, EVT_PUBLISHED_OK);

    int msg_id = esp_mqtt_client_publish(ctx->client, ctx->cfg.topic,
                                        payload, payload_len,
                                        ctx->cfg.qos, ctx->cfg.retain);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish failed (msg_id=%d)", msg_id);
        xSemaphoreGive(ctx->pub_mtx);
        return false;
    }

    ctx->awaiting_msg_id = msg_id;

    // Wait ACK signaled by mqtt_send_module_on_published()
    EventBits_t bits = xEventGroupWaitBits(ctx->ev, EVT_PUBLISHED_OK,
                                          pdTRUE, pdFALSE,
                                          pdMS_TO_TICKS(5000));
    bool ok = (bits & EVT_PUBLISHED_OK) != 0;

    if (!ok) {
        ESP_LOGW(TAG, "ACK timeout (msg_id=%d)", msg_id);
    }

    ctx->awaiting_msg_id = -1;
    xSemaphoreGive(ctx->pub_mtx);
    return ok;
}

// Build payload from the ring buffer without consuming.
// Returns how many items were included in payload.
static uint16_t _build_payload_locked(mqtt_send_module_ctx_t *ctx,
                                     char *payload, size_t payload_max,
                                     uint16_t max_items) {
    uint16_t n = 0;
    size_t len = 0;

    if (payload_max < 64) return 0;

    if (ctx->cfg.json_array_mode) {
        payload[len++] = '[';
    }

    uint16_t idx = ctx->head;
    uint16_t available = ctx->count;
    uint16_t to_take = _min_u16(available, max_items);

    for (uint16_t i = 0; i < to_take; i++) {
        const mqtt_send_sample_t *s = &ctx->buf[idx];

        // One telemetry item (controlled size)
        // Adjust rpm precision if you want.
        char item[264];
        int item_len = snprintf(item, sizeof(item),
                                "{\"ts\":%lld,\"front\":%u,\"back\":%u,\"state\":\"%s\",\"rpm\":%.2f,\"timestamp_rtc\":\"%s\"}",
                                (long long)s->ts_ms,
                                (unsigned)s->front_percent,
                                (unsigned)s->back_percent,
                                _state_to_str(s->state),
                                (double)s->rpm,
                                s->timestamp_rtc);

        if (item_len <= 0 || (size_t)item_len >= sizeof(item)) {
            idx = (uint16_t)((idx + 1) % ctx->cfg.capacity_items);
            continue;
        }

        // Extra bytes including separators
        size_t extra = (size_t)item_len + 2;
        if (ctx->cfg.json_array_mode) {
            extra += (n > 0) ? 1 : 0; // comma
        } else {
            extra += 1; // '\n'
        }

        if ((len + extra + 2) >= payload_max) {
            break; // no more space
        }

        if (ctx->cfg.json_array_mode) {
            if (n > 0) payload[len++] = ',';
            memcpy(&payload[len], item, (size_t)item_len);
            len += (size_t)item_len;
        } else {
            memcpy(&payload[len], item, (size_t)item_len);
            len += (size_t)item_len;
            payload[len++] = '\n';
        }

        n++;
        idx = (uint16_t)((idx + 1) % ctx->cfg.capacity_items);
    }

    if (n == 0) return 0;

    if (ctx->cfg.json_array_mode) {
        payload[len++] = ']';
    }
    payload[len] = '\0';
    return n;
}

// Consume (remove) n items from the ring buffer
static void _consume_n_locked(mqtt_send_module_ctx_t *ctx, uint16_t n) {
    if (n == 0 || ctx->count == 0) return;

    if (n >= ctx->count) {
        ctx->head = 0;
        ctx->tail = 0;
        ctx->count = 0;
        return;
    }

    ctx->head = (uint16_t)((ctx->head + n) % ctx->cfg.capacity_items);
    ctx->count = (uint16_t)(ctx->count - n);
}

static void _task_main(void *arg) {
    mqtt_send_module_ctx_t *ctx = (mqtt_send_module_ctx_t*)arg;

    TickType_t last_flush = xTaskGetTickCount();
    const TickType_t flush_period = pdMS_TO_TICKS(ctx->cfg.flush_period_ms);

    char *payload = (char*)malloc(ctx->cfg.payload_bytes_max + 8);
    if (!payload) {
        ESP_LOGE(TAG, "out of memory for payload");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // Wait for data / forced flush / short timeout to track flush_period
        (void)xEventGroupWaitBits(ctx->ev,
                                 EVT_HAVE_DATA | EVT_FORCE_FLUSH,
                                 pdTRUE, pdFALSE,
                                 pdMS_TO_TICKS(200));

        TickType_t now = xTaskGetTickCount();
        bool time_to_flush = (now - last_flush) >= flush_period;

        uint16_t current_count = 0;
        if (xSemaphoreTake(ctx->mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
            current_count = ctx->count;
            xSemaphoreGive(ctx->mtx);
        }

        EventBits_t bits_now = xEventGroupGetBits(ctx->ev);
        bool forced = (bits_now & EVT_FORCE_FLUSH) != 0;

        bool should_try = forced ||
                          (current_count >= ctx->cfg.batch_items) ||
                          (time_to_flush && current_count > 0);

        if (!should_try) {
            continue;
        }

        last_flush = now;

        if (!ctx->connected) {
            // offline -> let the queue grow
            continue;
        }

        // Build payload without consuming
        uint16_t n_payload = 0;
        if (xSemaphoreTake(ctx->mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
            n_payload = _build_payload_locked(ctx, payload, ctx->cfg.payload_bytes_max, ctx->cfg.batch_items);
            xSemaphoreGive(ctx->mtx);
        }

        if (n_payload == 0) {
            continue;
        }

        bool ok = _publish_and_wait_ack(ctx, payload, (int)strlen(payload));

        if (ok) {
            if (xSemaphoreTake(ctx->mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
                _consume_n_locked(ctx, n_payload);
                uint16_t remain = ctx->count;
                xSemaphoreGive(ctx->mtx);

                if (remain > 0) {
                    xEventGroupSetBits(ctx->ev, EVT_HAVE_DATA);
                }
            }
        } else {
            // Backoff to avoid hammering the broker
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Keep trying
            xEventGroupSetBits(ctx->ev, EVT_HAVE_DATA);
        }
    }

    // never reached
    // free(payload);
}

mqtt_send_module_ctx_t *mqtt_send_module_init(
    const mqtt_send_module_config_t *cfg,
    esp_mqtt_client_handle_t mqtt_client
) {
    if (!cfg || !mqtt_client || !cfg->topic) return NULL;
    if (cfg->capacity_items == 0 || cfg->batch_items == 0) return NULL;
    if (cfg->payload_bytes_max < 256) return NULL;

    mqtt_send_module_ctx_t *ctx = (mqtt_send_module_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->cfg = *cfg;
    ctx->client = mqtt_client;

    ctx->buf = (mqtt_send_sample_t*)calloc(cfg->capacity_items, sizeof(mqtt_send_sample_t));
    ctx->mtx = xSemaphoreCreateMutex();
    ctx->ev  = xEventGroupCreate();
    ctx->pub_mtx = xSemaphoreCreateMutex();

    ctx->connected = false;
    ctx->awaiting_msg_id = -1;

    if (!ctx->buf || !ctx->mtx || !ctx->ev || !ctx->pub_mtx) {
        mqtt_send_module_deinit(ctx);
        return NULL;
    }

    const char *tname = cfg->task_name ? cfg->task_name : "mqtt_send_module";
    uint32_t stack = cfg->task_stack_words ? cfg->task_stack_words : 4096;
    UBaseType_t prio = cfg->task_priority ? cfg->task_priority : 5;

    if (cfg->task_core >= 0) {
        xTaskCreatePinnedToCore(_task_main, tname, stack, ctx, prio, &ctx->task, cfg->task_core);
    } else {
        xTaskCreate(_task_main, tname, stack, ctx, prio, &ctx->task);
    }

    return ctx;
}

void mqtt_send_module_set_connected(mqtt_send_module_ctx_t *ctx, bool connected) {
    if (!ctx) return;
    ctx->connected = connected;

    if (connected) {
        xEventGroupSetBits(ctx->ev, EVT_CONNECTED | EVT_HAVE_DATA);
    } else {
        xEventGroupClearBits(ctx->ev, EVT_CONNECTED);
    }
}

void mqtt_send_module_on_published(mqtt_send_module_ctx_t *ctx, int msg_id) {
    if (!ctx) return;

    int awaiting = ctx->awaiting_msg_id;
    if (awaiting >= 0 && msg_id == awaiting) {
        xEventGroupSetBits(ctx->ev, EVT_PUBLISHED_OK);
    }
}

bool mqtt_send_module_push_sample(mqtt_send_module_ctx_t *ctx,
                                 const mqtt_send_sample_t *s) {
    if (!ctx || !s) return false;

    bool ok = false;

    if (xSemaphoreTake(ctx->mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (ctx->count < ctx->cfg.capacity_items) {
            ctx->buf[ctx->tail] = *s;
            ctx->tail = (uint16_t)((ctx->tail + 1) % ctx->cfg.capacity_items);
            ctx->count++;
            ok = true;
        } else {
            // full queue
            if (ctx->cfg.drop_oldest_on_full) {
                // overwrite oldest (drop-oldest)
                ctx->buf[ctx->tail] = *s;
                ctx->tail = (uint16_t)((ctx->tail + 1) % ctx->cfg.capacity_items);
                ctx->head = ctx->tail; // keep count == capacity
                ctx->count = ctx->cfg.capacity_items;
                ok = true;
            } else {
                // drop-newest
                ok = false;
            }
        }
        xSemaphoreGive(ctx->mtx);
    }

    if (ok) {
        xEventGroupSetBits(ctx->ev, EVT_HAVE_DATA);

        // If batch threshold reached, wake sender earlier
        if (mqtt_send_module_count(ctx) >= ctx->cfg.batch_items) {
            xEventGroupSetBits(ctx->ev, EVT_FORCE_FLUSH);
        }
    }

    return ok;
}

void mqtt_send_module_request_flush(mqtt_send_module_ctx_t *ctx) {
    if (!ctx) return;
    xEventGroupSetBits(ctx->ev, EVT_FORCE_FLUSH);
}

uint16_t mqtt_send_module_count(mqtt_send_module_ctx_t *ctx) {
    if (!ctx) return 0;

    uint16_t c = 0;
    if (xSemaphoreTake(ctx->mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
        c = ctx->count;
        xSemaphoreGive(ctx->mtx);
    }
    return c;
}

bool mqtt_send_module_is_connected(mqtt_send_module_ctx_t *ctx) {
    return ctx ? ctx->connected : false;
}

void mqtt_send_module_deinit(mqtt_send_module_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->task) {
        vTaskDelete(ctx->task);
        ctx->task = NULL;
    }
    if (ctx->buf) free(ctx->buf);
    if (ctx->mtx) vSemaphoreDelete(ctx->mtx);
    if (ctx->pub_mtx) vSemaphoreDelete(ctx->pub_mtx);
    if (ctx->ev) vEventGroupDelete(ctx->ev);

    free(ctx);
}
