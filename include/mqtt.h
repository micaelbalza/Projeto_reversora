#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lwip/err.h"   // define err_t

// Retorna 0 em sucesso; buffer opcional para mensagens (pode ser NULL)
int  mqtt_setup(const char *client_id, const char *broker_ip,
                const char *user, const char *pass, char *text_buffer);

// Publica (QoS 1, retain 1). Retorna ERR_OK em sucesso.
err_t mqtt_comm_publish(const char *topic, const uint8_t *data, size_t len);

// Indica se está conectado ao broker.
bool mqtt_is_connected(void);

typedef void (*mqtt_puback_cb_t)(void);

// Registra callback chamado quando um publish QoS1 recebe PUBACK (success).
void mqtt_set_puback_callback(mqtt_puback_cb_t cb);

// Opcional: consulta se há publish QoS1 em voo (true = aguardando PUBACK)
bool mqtt_publish_inflight(void);

#endif

