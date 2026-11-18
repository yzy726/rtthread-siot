/* Copyright (c) 2026 yzy726
 * SPDX-License-Identifier: Apache-2.0
 * This optional example has no board, sensor or Wi-Fi driver dependency.
 */
#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>
#include <string.h>
#include "siot_client.h"
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
    config.client_id = SIOT_EXAMPLE_CLIENT_ID;
    config.username = SIOT_EXAMPLE_USERNAME;
    config.password = SIOT_EXAMPLE_PASSWORD;
    config.keepalive = 60;
    config.on_message = received;
#ifdef SIOT_USING_TLS
    config.use_tls = RT_TRUE;
    config.ca_pem = siot_example_ca_pem;
#endif
    siot_client_init(&example_client);
    while (!example_stopping)
    {
        result = siot_client_connect(&example_client, &config);
        if (!result) result = siot_client_subscribe(&example_client, SIOT_EXAMPLE_SUB_TOPIC);
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
                result = siot_client_publish(&example_client, SIOT_EXAMPLE_PUB_TOPIC,
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
    if (!SIOT_EXAMPLE_HOST[0] || !SIOT_EXAMPLE_CLIENT_ID[0] ||
        !SIOT_EXAMPLE_SUB_TOPIC[0] || !SIOT_EXAMPLE_PUB_TOPIC[0])
    {
        rt_kprintf("[siot] configure the example host, client ID and topics first\n");
        return -RT_EINVAL;
    }
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
    /* Thread allocation happens after interrupts have been restored. */
    thread = rt_thread_create("siotdemo", example_entry, RT_NULL, 8192, 20, 10);
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
