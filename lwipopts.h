// lwipopts.h — Pico W + lwIP + MQTT (bare-metal, no FreeRTOS)
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// ===== Núcleo/arquitetura =====
#define NO_SYS                         1         // sem RTOS
#define MEM_LIBC_MALLOC                0
#define MEM_ALIGNMENT                  4

// ===== Memória e pools =====
// Aumente MEM_SIZE e o pool de timeouts para evitar panic em conexões TCP/MQTT.
#define MEM_SIZE                       (32 * 1024)
#define MEMP_NUM_SYS_TIMEOUT           24        // crítico p/ TCP/DHCP/DNS/MQTT timers
#define MEMP_NUM_TCP_SEG               32
#define MEMP_NUM_ARP_QUEUE             10
#define PBUF_POOL_SIZE                 24

// ===== Protocolos/recursos =====
#define LWIP_IPV4                      1
#define LWIP_ETHERNET                  1
#define LWIP_ARP                       1
#define LWIP_ICMP                      1
#define LWIP_RAW                       1
#define LWIP_TCP                       1
#define LWIP_UDP                       1
#define LWIP_DNS                       1
#define LWIP_DHCP                      1
#define LWIP_TCP_KEEPALIVE             1

// MQTT do lwIP (cliente em lwip/apps/mqtt)
#define LWIP_MQTT                      1
// (opcional) buffers do MQTT um pouco maiores:
#define MQTT_OUTPUT_RINGBUF_SIZE       1024
#define MQTT_REQ_MAX_IN_FLIGHT         4

// ===== API superiores (não usadas no bare-metal) =====
#define LWIP_SOCKET                    0
#define LWIP_NETCONN                   0

// ===== Rede/Netif =====
#define LWIP_NETIF_STATUS_CALLBACK     1
#define LWIP_NETIF_LINK_CALLBACK       1
#define LWIP_NETIF_HOSTNAME            1
#define LWIP_NETIF_TX_SINGLE_PBUF      1

// ===== DHCP/Duplicate Address Detection =====
#define DHCP_DOES_ARP_CHECK            0
#define LWIP_DHCP_DOES_ACD_CHECK       0

// ===== TCP tunings (equilíbrio memória/latência) =====
#define TCP_MSS                        1460
#define TCP_WND                        (8 * TCP_MSS)
#define TCP_SND_BUF                    (8 * TCP_MSS)
#define TCP_SND_QUEUELEN               16

// ===== Checksums =====
#define LWIP_CHKSUM_ALGORITHM          3

// ===== Estatísticas / debug =====
#define MEM_STATS                      0
#define SYS_STATS                      0
#define MEMP_STATS                     0
#define LINK_STATS                     0

#ifndef NDEBUG
  #define LWIP_DEBUG                   0   // mude p/ 1 se quiser logs do lwIP
  #define LWIP_STATS                   0
  #define LWIP_STATS_DISPLAY           0
#endif

// (mantém tudo de debug específico em OFF)
#define ETHARP_DEBUG                   LWIP_DBG_OFF
#define NETIF_DEBUG                    LWIP_DBG_OFF
#define PBUF_DEBUG                     LWIP_DBG_OFF
#define API_LIB_DEBUG                  LWIP_DBG_OFF
#define API_MSG_DEBUG                  LWIP_DBG_OFF
#define SOCKETS_DEBUG                  LWIP_DBG_OFF
#define ICMP_DEBUG                     LWIP_DBG_OFF
#define INET_DEBUG                     LWIP_DBG_OFF
#define IP_DEBUG                       LWIP_DBG_OFF
#define IP_REASS_DEBUG                 LWIP_DBG_OFF
#define RAW_DEBUG                      LWIP_DBG_OFF
#define MEM_DEBUG                      LWIP_DBG_OFF
#define MEMP_DEBUG                     LWIP_DBG_OFF
#define SYS_DEBUG                      LWIP_DBG_OFF
#define TCP_DEBUG                      LWIP_DBG_OFF
#define TCP_INPUT_DEBUG                LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG               LWIP_DBG_OFF
#define TCP_RTO_DEBUG                  LWIP_DBG_OFF
#define TCP_CWND_DEBUG                 LWIP_DBG_OFF
#define TCP_WND_DEBUG                  LWIP_DBG_OFF
#define TCP_FR_DEBUG                   LWIP_DBG_OFF
#define TCP_QLEN_DEBUG                 LWIP_DBG_OFF
#define TCP_RST_DEBUG                  LWIP_DBG_OFF
#define UDP_DEBUG                      LWIP_DBG_OFF
#define TCPIP_DEBUG                    LWIP_DBG_OFF
#define PPP_DEBUG                      LWIP_DBG_OFF
#define SLIP_DEBUG                     LWIP_DBG_OFF
#define DHCP_DEBUG                     LWIP_DBG_OFF

#endif /* LWIPOPTS_H */
