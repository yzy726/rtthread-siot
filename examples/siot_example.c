/* Copyright (c) 2026 yzy726
 * SPDX-License-Identifier: Apache-2.0
 * This optional example has no board, sensor or Wi-Fi driver dependency.
 */
#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>
#include <string.h>
#include "siot_client.h"
#ifdef SIOT_USING_CHANNELS
#include "siot_channels.h"
static struct siot_channels channels;
#endif
#ifdef SIOT_USING_TLS
#include "siot_example_ca.h"
#endif

static struct siot_client example_client;
static volatile rt_bool_t example_running;
static volatile rt_bool_t example_stopping;

static void received(void *user, const char *topic, int topic_size,
                     const void *payload, int payload_size, int retained)
{
    (void)user;
#ifdef SIOT_USING_CHANNELS
    {
        int route = siot_channels_match(&channels, topic, topic_size, retained);
        if (route <= 0) return;
        rt_kprintf("[siot] matched command route=%d (demo only, no actuator)\n", route);
    }
#endif
    rt_kprintf("[siot] topic=%.*s payload=%.*s retained=%d\n",
               topic_size > 80 ? 80 : topic_size, topic,
               payload_size > 100 ? 100 : payload_size, (const char *)payload, retained);
}

static void example_entry(void *parameter)
{
    struct siot_config config;
    rt_tick_t last_publish = 0;
    rt_bool_t first;
    int result;
    rt_base_t level;
    static const char message[] = "hello from RT-Thread";
    (void)parameter;
    memset(&config, 0, sizeof(config));
    config.host = SIOT_EXAMPLE_HOST;
    config.port = SIOT_EXAMPLE_PORT;
#ifdef SIOT_USING_CHANNELS
    config.client_id = channels.prefix;
#else
    config.client_id = SIOT_EXAMPLE_CLIENT_ID;
#endif
    config.username = SIOT_EXAMPLE_USERNAME;
    config.password = SIOT_EXAMPLE_PASSWORD;
    config.keepalive = 60;
    config.on_message = received;
#ifdef SIOT_EXAMPLE_PUBLISH_QOS0
    config.publish_qos0 = RT_TRUE;
#endif
#ifdef SIOT_USING_TLS
    config.use_tls = RT_TRUE;
    config.ca_pem = siot_example_ca_pem;
#endif
    siot_client_init(&example_client);
    while (!example_stopping)
    {
        result = siot_client_connect(&example_client, &config);
        if (!result)
#ifdef SIOT_USING_CHANNELS
            result = siot_channels_subscribe(&example_client, &channels);
#else
            result = siot_client_subscribe(&example_client, SIOT_EXAMPLE_SUB_TOPIC);
#endif
        if (result)
        {
            rt_kprintf("[siot] connect/subscribe error=%d detail=%d; retry in 5s\n",
                       result, example_client.last_error);
            siot_client_disconnect(&example_client);
            rt_thread_mdelay(5000);
            continue;
        }
        rt_kprintf("[siot] connected, subscription ready\n");
        first = RT_TRUE;
        while (!example_stopping && example_client.connected)
        {
            result = siot_client_poll(&example_client, 100);
            if (result) break;
            if (first || (rt_tick_t)(rt_tick_get() - last_publish) >= rt_tick_from_millisecond(5000))
            {
#ifdef SIOT_USING_CHANNELS
                result = siot_client_publish(&example_client, channels.state,
#else
                result = siot_client_publish(&example_client, SIOT_EXAMPLE_PUB_TOPIC,
#endif
                                             message, sizeof(message) - 1);
                if (result) break;
                first = RT_FALSE;
                last_publish = rt_tick_get();
            }
        }
        siot_client_disconnect(&example_client);
        if (!example_stopping) rt_thread_mdelay(5000);
    }
    siot_client_disconnect(&example_client);
    level = rt_hw_interrupt_disable();
    example_running = RT_FALSE;
    rt_hw_interrupt_enable(level);
    rt_kprintf("[siot] stopped\n");
}

static int siot_start(void)
{
    rt_thread_t thread;
    rt_base_t level;
    if (!SIOT_EXAMPLE_HOST[0]) return -RT_EINVAL;
#ifndef SIOT_USING_CHANNELS
    if (!SIOT_EXAMPLE_CLIENT_ID[0] ||
        !SIOT_EXAMPLE_SUB_TOPIC[0] || !SIOT_EXAMPLE_PUB_TOPIC[0])
    {
        rt_kprintf("[siot] configure the example host, client ID and topics first\n");
        return -RT_EINVAL;
    }
#endif
    level = rt_hw_interrupt_disable();
    if (example_running)
    {
        rt_hw_interrupt_enable(level);
        rt_kprintf("[siot] already running or stopping\n");
        return -RT_EBUSY;
    }
    example_running = RT_TRUE;
    example_stopping = RT_FALSE;
    rt_hw_interrupt_enable(level);
#ifdef SIOT_USING_CHANNELS
    if (siot_channels_init_from_config(&channels))
    {
        example_running = RT_FALSE;
        rt_kprintf("[siot] invalid channel IDs\n");
        return -RT_EINVAL;
    }
#endif
    /* Thread allocation happens after interrupts have been restored. */
    thread = rt_thread_create("siotdemo", example_entry, RT_NULL, 8192,
                              RT_THREAD_PRIORITY_MAX / 2, 10);
    if (!thread)
    {
        example_running = RT_FALSE;
        return -RT_ENOMEM;
    }
    return rt_thread_startup(thread);
}
MSH_CMD_EXPORT(siot_start, Connect to your configured SIoT and publish a demo message);

static void siot_stop(void)
{
    example_stopping = RT_TRUE;
    rt_kprintf("[siot] stop requested; wait for the current network timeout\n");
}
MSH_CMD_EXPORT(siot_stop, Request the SIoT example worker to disconnect and exit);
