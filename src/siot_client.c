/* SPDX-License-Identifier: Apache-2.0
 * Small synchronous MQTT adapter for RT-Thread/SAL and Paho MQTTPacket.
 * All socket operations belong to the caller; no hidden worker or shared buffer.
 */
#include "siot_client.h"
#include <MQTTPacket.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#ifdef SIOT_USING_TLS
#include <mbedtls/ssl.h>
#include <mbedtls/net.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <time.h>
struct siot_tls
{
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
};
#endif

#define IO_TIMEOUT_MS 5000
static int fail(struct siot_client *c, int error)
{
    c->last_error = error;
    siot_client_disconnect(c);
    return error;
}
static int wait_socket(int fd, int write_ready, int timeout_ms)
{
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(fd + 1, write_ready ? RT_NULL : &fds,
                  write_ready ? &fds : RT_NULL, RT_NULL, &tv);
}
static int tcp_connect(const char *host, unsigned short port)
{
    struct addrinfo hints, *addresses = RT_NULL, *p;
    char port_text[6];
    int fd = -1, status, error;
    unsigned long nonblock = 1;
    socklen_t error_size;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%u", port);
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0) return -1;
    for (p = addresses; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (ioctlsocket(fd, FIONBIO, &nonblock) < 0) { closesocket(fd); fd = -1; continue; }
        status = connect(fd, p->ai_addr, p->ai_addrlen);
        if (status == 0) break;
        if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
        {
            error = -1;
            error_size = sizeof(error);
            if (wait_socket(fd, 1, IO_TIMEOUT_MS) > 0 &&
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) == 0 && error == 0) break;
        }
        closesocket(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}
#ifdef SIOT_USING_TLS
static int tls_send(void *ctx, const unsigned char *data, size_t len)
{
    struct siot_client *c = ctx;
    int n = send(c->socket, data, len, 0);
    if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return n < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : n;
}
static int tls_recv(void *ctx, unsigned char *data, size_t len)
{
    struct siot_client *c = ctx;
    int n = recv(c->socket, data, len, 0);
    if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) return MBEDTLS_ERR_SSL_WANT_READ;
    return n < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : n;
}
static int tls_connect(struct siot_client *c)
{
    struct siot_tls *t;
    rt_tick_t start;
    int result;
    static const unsigned char personalization[] = "RT-Thread-SIoT";
    /* Certificate expiry verification requires a real UTC clock. */
    if (time(RT_NULL) < 1735689600 || !c->config.ca_pem || !c->config.ca_pem[0]) return -RT_EINVAL;
    t = rt_calloc(1, sizeof(*t));
    if (!t) return -RT_ENOMEM;
    c->tls = t;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_x509_crt_init(&t->ca);
    result = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
                                 personalization, sizeof(personalization));
    if (result) return result;
    result = mbedtls_x509_crt_parse(&t->ca, (const unsigned char *)c->config.ca_pem,
                                  strlen(c->config.ca_pem) + 1);
    if (result) return result;
    result = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result) return result;
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, RT_NULL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    result = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (result) return result;
    result = mbedtls_ssl_set_hostname(&t->ssl, c->config.host);
    if (result) return result;
    mbedtls_ssl_set_bio(&t->ssl, c, tls_send, tls_recv, RT_NULL);
    start = rt_tick_get();
    while ((result = mbedtls_ssl_handshake(&t->ssl)) != 0)
    {
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return result;
        if ((rt_tick_t)(rt_tick_get() - start) > rt_tick_from_millisecond(20000)) return -RT_ETIMEOUT;
        if (wait_socket(c->socket, result == MBEDTLS_ERR_SSL_WANT_WRITE, 200) < 0) return -RT_ERROR;
    }
    return mbedtls_ssl_get_verify_result(&t->ssl) == 0 ? 0 : -RT_ERROR;
}
#endif
static int transfer(struct siot_client *c, unsigned char *data, int count, int writing)
{
    int done = 0, n, direction;
    rt_tick_t start = rt_tick_get();
    while (done < count)
    {
        direction = writing;
#ifdef SIOT_USING_TLS
        if (c->tls)
        {
            struct siot_tls *t = c->tls;
            n = writing ? mbedtls_ssl_write(&t->ssl, data + done, count - done) :
                          mbedtls_ssl_read(&t->ssl, data + done, count - done);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            { direction = n == MBEDTLS_ERR_SSL_WANT_WRITE; n = -2; }
            else if (n < 0) { c->last_error = n; return -1; }
        }
        else
#endif
        {
            n = writing ? send(c->socket, data + done, count - done, 0) :
                          recv(c->socket, data + done, count - done, 0);
            if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) n = -2;
            else if (n < 0) return -1;
        }
        if (n == 0) return -1;
        if (n > 0) done += n;
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(IO_TIMEOUT_MS)) return -1;
        if (n == -2 && wait_socket(c->socket, direction, 100) < 0) return -1;
    }
    if (writing) c->last_io = rt_tick_get(); /* MQTT keepalive measures sent packets. */
    return done;
}
static int send_packet(struct siot_client *c, int length)
{
    if (length <= 0 || transfer(c, c->tx, length, 1) != length) return fail(c, -RT_ERROR);
    return 0;
}
/* Read exactly one bounded MQTT packet. A partial frame timeout closes the
 * connection, so bytes from different frames can never be joined accidentally. */
static int read_packet(struct siot_client *c, int timeout_ms)
{
    unsigned int remaining = 0, multiplier = 1;
    unsigned int topic_size, minimum;
    int i = 1, ready, type, qos;
    unsigned char digit;
#ifdef SIOT_USING_TLS
    struct siot_tls *t = c->tls;
    ready = t && mbedtls_ssl_get_bytes_avail(&t->ssl) ? 1 : wait_socket(c->socket, 0, timeout_ms);
#else
    ready = wait_socket(c->socket, 0, timeout_ms);
#endif
    if (ready == 0) return 0;
    if (ready < 0 || transfer(c, c->rx, 1, 0) != 1) return fail(c, -RT_ERROR);
    do
    {
        if (i > 4 || transfer(c, &digit, 1, 0) != 1) return fail(c, -RT_ERROR);
        c->rx[i++] = digit;
        remaining += (digit & 127u) * multiplier;
        multiplier *= 128;
    } while (digit & 128u);
    if (remaining > sizeof(c->rx) - i) return fail(c, -RT_EFULL);
    if (remaining && transfer(c, c->rx + i, remaining, 0) != (int)remaining) return fail(c, -RT_ERROR);
    c->rx_length = i + remaining;
    type = c->rx[0] >> 4;
    /* Validate lengths/flags before calling the legacy Paho decoder, which
     * assumes a complete valid packet and does not check every field boundary. */
    if (type == PUBLISH)
    {
        qos = (c->rx[0] >> 1) & 3;
        if (qos > 1 || remaining < 2) return fail(c, -RT_ERROR);
        topic_size = ((unsigned int)c->rx[i] << 8) | c->rx[i+1];
        minimum = 2 + topic_size + (qos ? 2 : 0);
        if (!topic_size || minimum > remaining || memchr(c->rx+i+2, 0, topic_size) ||
            memchr(c->rx+i+2, '#', topic_size) || memchr(c->rx+i+2, '+', topic_size)) return fail(c, -RT_ERROR);
        if (qos && !(c->rx[i+2+topic_size] || c->rx[i+3+topic_size])) return fail(c, -RT_ERROR);
    }
    else
    {
        if ((c->rx[0] & 15) != 0) return fail(c, -RT_ERROR);
        if ((type == CONNACK && remaining != 2) || (type == PUBACK && remaining != 2) ||
            (type == SUBACK && remaining != 3) ||
            ((type == PINGRESP || type == DISCONNECT) && remaining != 0)) return fail(c, -RT_ERROR);
        if (type != CONNACK && type != PUBACK && type != SUBACK && type != PINGRESP && type != DISCONNECT)
            return fail(c, -RT_ERROR);
    }
    return type;
}
static unsigned short next_id(struct siot_client *c)
{
    if (++c->packet_id == 0) ++c->packet_id;
    return c->packet_id;
}
static int dispatch(struct siot_client *c, int type)
{
    if (type == PUBLISH)
    {
        MQTTString topic = MQTTString_initializer;
        unsigned char dup, retained, *payload;
        unsigned short id;
        int qos, length;
        if (!MQTTDeserialize_publish(&dup, &qos, &retained, &id, &topic, &payload, &length,
                                     c->rx, c->rx_length) || qos > 1 || length < 0) return fail(c, -RT_ERROR);
        if (c->config.on_message)
            c->config.on_message(c->config.user, topic.lenstring.data, topic.lenstring.len,
                                 payload, length, retained);
        if (qos == 1) return send_packet(c, MQTTSerialize_ack(c->tx, sizeof(c->tx), PUBACK, 0, id));
    }
    else if (type == PINGRESP) c->ping_at = 0;
    else if (type == DISCONNECT) return fail(c, -RT_ERROR);
    return 0;
}
static int await_ack(struct siot_client *c, int expected, unsigned short id)
{
    rt_tick_t start = rt_tick_get();
    int type, count, granted;
    unsigned short received;
    unsigned char packet_type, dup;
    while ((rt_tick_t)(rt_tick_get() - start) < rt_tick_from_millisecond(IO_TIMEOUT_MS))
    {
        type = read_packet(c, 200);
        if (type < 0) return type;
        if (type == expected)
        {
            if (type == SUBACK)
            {
                if (MQTTDeserialize_suback(&received, 1, &count, &granted, c->rx, c->rx_length) != 1 ||
                    received != id || count != 1 || granted < 0 || granted > 1) return fail(c, -RT_ERROR);
            }
            else if (!MQTTDeserialize_ack(&packet_type, &dup, &received, c->rx, c->rx_length) || received != id)
                return fail(c, -RT_ERROR);
            return 0;
        }
        if (dispatch(c, type) < 0) return -RT_ERROR;
    }
    return fail(c, -RT_ETIMEOUT);
}
void siot_client_init(struct siot_client *c)
{
    memset(c, 0, sizeof(*c));
    c->socket = -1;
}
void siot_client_disconnect(struct siot_client *c)
{
#ifdef SIOT_USING_TLS
    if (c->tls)
    {
        struct siot_tls *t = c->tls;
        mbedtls_ssl_free(&t->ssl);
        mbedtls_ssl_config_free(&t->conf);
        mbedtls_x509_crt_free(&t->ca);
        mbedtls_ctr_drbg_free(&t->drbg);
        mbedtls_entropy_free(&t->entropy);
        rt_free(t);
        c->tls = RT_NULL;
    }
#endif
    if (c->socket >= 0) closesocket(c->socket);
    c->socket = -1;
    c->connected = 0;
    c->ping_at = 0;
}
int siot_client_connect(struct siot_client *c, const struct siot_config *config)
{
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    unsigned char present, code;
    int type;
    if (!c || !config || !config->host || !config->host[0] || !config->client_id ||
        !config->client_id[0] || config->keepalive < 10 || !config->port) return -RT_EINVAL;
    siot_client_disconnect(c);
    c->config = *config;
    c->last_error = 0;
#ifndef SIOT_USING_TLS
    if (config->use_tls) return fail(c, -RT_ENOSYS);
#endif
    c->socket = tcp_connect(config->host, config->port);
    if (c->socket < 0) return fail(c, -RT_ERROR);
#ifdef SIOT_USING_TLS
    if (config->use_tls)
    {
        int result = tls_connect(c);
        if (result) return fail(c, result);
    }
#endif
    data.MQTTVersion = 4;
    data.clientID.cstring = (char *)config->client_id;
    data.username.cstring = config->username && config->username[0] ? (char *)config->username : RT_NULL;
    data.password.cstring = config->password && config->password[0] ? (char *)config->password : RT_NULL;
    data.keepAliveInterval = config->keepalive;
    data.cleansession = 1;
    if (send_packet(c, MQTTSerialize_connect(c->tx, sizeof(c->tx), &data)) < 0) return -RT_ERROR;
    type = read_packet(c, IO_TIMEOUT_MS);
    if (type != CONNACK || !MQTTDeserialize_connack(&present, &code, c->rx, c->rx_length))
        return fail(c, -RT_ERROR);
    /* -101..-105 preserves broker connection refusal reason. */
    if (code) return fail(c, -100 - code);
    c->connected = 1;
    return 0;
}
int siot_client_subscribe(struct siot_client *c, const char *filter)
{
    MQTTString topic = MQTTString_initializer;
    unsigned short id;
    int qos = 1;
    if (!c->connected || !filter || !filter[0]) return -RT_EINVAL;
    topic.cstring = (char *)filter;
    id = next_id(c);
    if (send_packet(c, MQTTSerialize_subscribe(c->tx, sizeof(c->tx), 0, id, 1, &topic, &qos)) < 0) return -RT_ERROR;
    return await_ack(c, SUBACK, id);
}
int siot_client_publish(struct siot_client *c, const char *name, const void *payload, int size)
{
    MQTTString topic = MQTTString_initializer;
    unsigned short id;
    if (!c->connected || !name || !name[0] || !payload || size < 0) return -RT_EINVAL;
    if (size + strlen(name) + 10 > sizeof(c->tx)) return -RT_EFULL;
    topic.cstring = (char *)name;
    id = c->config.publish_qos0 ? 0 : next_id(c);
    if (send_packet(c, MQTTSerialize_publish(c->tx, sizeof(c->tx), 0, c->config.publish_qos0 ? 0 : 1, 0, id, topic,
                                            (unsigned char *)payload, size)) < 0) return -RT_ERROR;
    return c->config.publish_qos0 ? 0 : await_ack(c, PUBACK, id);
}
int siot_client_poll(struct siot_client *c, int timeout_ms)
{
    int type;
    rt_tick_t now;
    if (!c->connected || timeout_ms < 0) return -RT_EINVAL;
    if (timeout_ms > 1000) timeout_ms = 1000;
    type = read_packet(c, timeout_ms);
    if (type < 0 || dispatch(c, type) < 0) return -RT_ERROR;
    now = rt_tick_get();
    if (c->ping_at && (rt_tick_t)(now - c->ping_at) > rt_tick_from_millisecond(IO_TIMEOUT_MS))
        return fail(c, -RT_ETIMEOUT);
    if (!c->ping_at && (rt_tick_t)(now - c->last_io) > rt_tick_from_millisecond(c->config.keepalive * 500))
    {
        if (send_packet(c, MQTTSerialize_pingreq(c->tx, sizeof(c->tx))) < 0) return -RT_ERROR;
        c->ping_at = rt_tick_get();
        if (!c->ping_at) c->ping_at = 1;
    }
    return 0;
}
