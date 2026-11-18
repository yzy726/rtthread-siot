/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SIOT_CLIENT_H
#define SIOT_CLIENT_H
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SIOT_MQTT_BUFFER_SIZE
#define SIOT_MQTT_BUFFER_SIZE 2048
#endif

/* One thread owns a client. Callbacks run in siot_client_poll(), never in ISR.
 * Topic and payload are length-delimited and valid only during the callback.
 * QoS 0/1, MQTT 3.1.1, clean session; retained commands can be rejected by app. */
typedef void (*siot_message_cb)(void *user, const char *topic, int topic_len,
                              const void *payload, int payload_len, int retained);
struct siot_config
{
    const char *host, *client_id, *username, *password;
    unsigned short port, keepalive;
    rt_bool_t use_tls;
    const char *ca_pem;
    siot_message_cb on_message;
    void *user;
    /* Explicit SIoT v1 compatibility: send QoS 0; success is socket delivery,
     * not PUBACK. Zero-initialized configurations keep the default QoS 1. */
    rt_bool_t publish_qos0;
};
struct siot_client
{
    int socket, connected, last_error;
    unsigned short packet_id;
    int rx_length;
    rt_tick_t last_io, ping_at;
    unsigned char tx[SIOT_MQTT_BUFFER_SIZE], rx[SIOT_MQTT_BUFFER_SIZE];
    struct siot_config config;
    void *tls;
};

void siot_client_init(struct siot_client *client);
int siot_client_connect(struct siot_client *client, const struct siot_config *config);
int siot_client_subscribe(struct siot_client *client, const char *topic);
/* Default success means PUBACK. With config.publish_qos0, success only means
 * the packet was sent; neither mode implies platform business acceptance. */
int siot_client_publish(struct siot_client *client, const char *topic,
                        const void *payload, int payload_len);
int siot_client_poll(struct siot_client *client, int timeout_ms);
void siot_client_disconnect(struct siot_client *client);
#ifdef __cplusplus
}
#endif
#endif
