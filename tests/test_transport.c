/* Exercises the actual target transport through a host socket shim. */
#include <sys/socket.h>
#include "siot_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <signal.h>
#endif
static unsigned int messages;
static void message(void *user, const char *topic, int topic_length, const void *data, int size, int retained)
{
    (void)user;
    if (topic_length == 10 && !memcmp(topic, "demo/value", 10) && size == 4 && !memcmp(data, "test", 4)) ++messages;
    if (retained) messages += 100;
}
int main(int argc, char **argv)
{
#ifdef _WIN32
    WSADATA wsa;
#endif
    struct siot_client c;
    struct siot_config config;
    int result, i, expected;
    if (argc != 3) return 2;
#ifdef _WIN32
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    memset(&config, 0, sizeof(config));
    config.host = "127.0.0.1"; config.port = (unsigned short)atoi(argv[1]);
    config.client_id = "host-regression"; config.keepalive = 10;
    config.username = "user"; config.password = "password"; config.on_message = message;
    config.publish_qos0 = !strcmp(argv[2], "qos0");
    siot_client_init(&c);
    result = siot_client_connect(&c, &config);
    if (!strcmp(argv[2], "refusal")) { siot_client_disconnect(&c); return result == -105 ? 0 : 10; }
    if (result) { fprintf(stderr, "connect failed: %d\n", result); return 11; }
    result = siot_client_subscribe(&c, "demo/value");
    if (!strcmp(argv[2], "subfail")) { siot_client_disconnect(&c); return result < 0 ? 0 : 12; }
    if (result) return 13;
    if (!strcmp(argv[2], "qos0"))
    {
        result = siot_client_publish(&c, "demo/value", "payload", 7);
        if (result) return 14;
        for (i = 0; i < 20 && messages < 1; ++i)
            if (siot_client_poll(&c, 100)) return 15;
        expected = messages == 1 && c.connected;
    }
    else if (!strcmp(argv[2], "normal"))
    {
        result = siot_client_publish(&c, "demo/value", "payload", 7);
        if (result) { fprintf(stderr, "publish failed: %d\n", result); return 14; }
        for (i = 0; i < 20 && messages < 2; ++i)
            if (siot_client_poll(&c, 100)) return 15;
        expected = messages == 2;
    }
    else if (!strcmp(argv[2], "missing-puback"))
    {
        result = siot_client_publish(&c, "demo/value", "payload", 7);
        expected = result < 0 && !c.connected;
    }
    else
    {
        result = 0;
        for (i = 0; i < 60 && result == 0; ++i) result = siot_client_poll(&c, 100);
        expected = result < 0 && !c.connected;
    }
    siot_client_disconnect(&c);
#ifdef _WIN32
    WSACleanup();
#endif
    return expected ? 0 : 16;
}
