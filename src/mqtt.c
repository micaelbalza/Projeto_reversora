#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/cyw43_arch.h"       // cyw43_arch_lwip_begin/end
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/apps/mqtt.h"

#include "mqtt.h"

static mqtt_client_t *client = NULL;
static volatile bool connected = false;
static volatile bool pub_in_flight = false;   // evita empilhar QoS1

// callback opcional informado pela aplicação (usado para liberar fila após PUBACK)
static mqtt_puback_cb_t s_puback_cb = NULL;

static void inpub_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    (void)arg; (void)flags;
    printf("MQTT RX payload: %.*s\n", len, (const char*)data);
}

static void subscribe_cb(void *arg, err_t result) {
    (void)arg;
    if (result == ERR_OK) printf("Subscribe OK\n");
    else                  printf("Subscribe ERR=%d\n", result);
}

// Exemplo de como inscrever (opcional, não usado agora)
static void subscribe_topic(mqtt_client_t *c, const char *topic) {
    mqtt_set_inpub_callback(c, NULL, inpub_data_cb, NULL);
    mqtt_sub_unsub(c, topic, 0 /*qos*/, subscribe_cb, NULL, 1 /*subscribe*/);
}

// Callback de conexão MQTT (thread do lwIP)
static void mqtt_connection_cb(mqtt_client_t *c, void *arg, mqtt_connection_status_t status) {
    (void)c; (void)arg;
    connected = (status == MQTT_CONNECT_ACCEPTED);
    if (connected) {
        pub_in_flight = false; // zera trava ao conectar
        printf("MQTT: conectado ao broker.\n");
        // subscribe_topic(client, "seu/topico/aqui");
    } else {
        printf("MQTT: falha na conexão (status=%d)\n", status);
    }
}

void mqtt_set_puback_callback(mqtt_puback_cb_t cb) {
    s_puback_cb = cb;
}

bool mqtt_publish_inflight(void) {
    return pub_in_flight;
}

bool mqtt_is_connected(void) {
    return client && mqtt_client_is_connected(client) && connected;
}

int mqtt_setup(const char *client_id, const char *broker_ip,
               const char *user, const char *pass, char *text_buffer) {
    if (client && mqtt_client_is_connected(client)) {
        if (text_buffer) snprintf(text_buffer, 100, "Ja conectado\n");
        return 0;
    }

    ip_addr_t broker_addr;
    if (text_buffer) snprintf(text_buffer, 100, "Conectando ao broker...\n");

    // Converte string -> IP (sem DNS aqui)
    if (!ip4addr_aton(broker_ip, &broker_addr)) {
        if (text_buffer) snprintf(text_buffer, 100, "Erro: IP invalido\n");
        return -1;
    }

    // Limpa instância anterior (se houver) PROTEGIDO
    if (client) {
        cyw43_arch_lwip_begin();
        mqtt_disconnect(client);
        mqtt_client_free(client);
        cyw43_arch_lwip_end();
        client = NULL;
    }

    // Cria cliente PROTEGIDO
    cyw43_arch_lwip_begin();
    client = mqtt_client_new();
    cyw43_arch_lwip_end();
    if (!client) {
        if (text_buffer) snprintf(text_buffer, 100, "Falha ao criar cliente MQTT\n");
        return -1;
    }

    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof(ci));
    ci.client_id   = client_id;
    ci.client_user = user;
    ci.client_pass = pass;
    ci.keep_alive  = 60;
    // (o lwIP do Pico SDK 2.2.0 não tem clean_session aqui)

    connected = false;
    pub_in_flight = false;

    // Conecta PROTEGIDO
    cyw43_arch_lwip_begin();
    err_t e = mqtt_client_connect(client, &broker_addr, 1883, mqtt_connection_cb, NULL, &ci);
    cyw43_arch_lwip_end();

    if (e != ERR_OK) {
        if (text_buffer) snprintf(text_buffer, 100, "mqtt_client_connect err=%d\n", e);
        return -1;
    }

    return 0;
}

// Confirmação da publicação (para QoS1) — chamado na thread do lwIP
static void mqtt_pub_request_cb(void *arg, err_t result) {
    (void)arg;
    if (result == ERR_OK) {
        printf("Publish confirmado (QoS1)\n");
        if (s_puback_cb) s_puback_cb();   // notifica aplicação (liberar fila/batch)
    } else {
        printf("Publish erro=%d\n", result);
    }
    pub_in_flight = false;  // libera o próximo publish
}

err_t mqtt_comm_publish(const char *topic, const uint8_t *data, size_t len) {
    if (!mqtt_is_connected()) return ERR_CONN;

    // Evita empilhar múltiplos publishes QoS1
    if (pub_in_flight) return ERR_INPROGRESS;
    pub_in_flight = true;

    const u8_t qos    = 1;  // QoS1
    const u8_t retain = 1;  // retido (mantenha se quiser replicar seu comportamento atual)

    // Publica PROTEGIDO
    cyw43_arch_lwip_begin();
    err_t st = mqtt_publish(
        client,
        topic,
        data,
        (u16_t)len,
        qos,
        retain,
        mqtt_pub_request_cb,
        NULL
    );
    cyw43_arch_lwip_end();

    if (st != ERR_OK) {
        printf("mqtt_publish falhou (err=%d) topic='%s'\n", st, topic);
        pub_in_flight = false; // falhou, libera
    }
    return st;
}
