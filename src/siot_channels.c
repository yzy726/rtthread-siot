/* SPDX-License-Identifier: Apache-2.0 */
#include "siot_channels.h"
#ifdef SIOT_USING_CHANNELS
#include <stdio.h>
#include <string.h>

static int alphanumeric(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
static int label_valid(const char *text)
{
    int i;
    if (!text || !alphanumeric((unsigned char)text[0])) return 0;
    for (i = 0; text[i]; ++i)
        if (i >= 24 || (!alphanumeric((unsigned char)text[i]) && text[i] != '-')) return 0;
    return 1;
}
unsigned int siot_channels_supported(void)
{
    unsigned int routes = 0;
#ifdef SIOT_CHANNEL_DEVICE
    routes |= SIOT_ROUTE_DEVICE;
#endif
#ifdef SIOT_CHANNEL_GROUP
    routes |= SIOT_ROUTE_GROUP;
#endif
#ifdef SIOT_CHANNEL_BROADCAST
    routes |= SIOT_ROUTE_BROADCAST;
#endif
    return routes;
}
int siot_channels_init(struct siot_channels *c, const char *project,
                       const char *device, const char *group, unsigned int routes)
{
    if (!c) return -RT_EINVAL;
    memset(c, 0, sizeof(*c));
    if (!label_valid(project) || !label_valid(device) ||
        (routes & ~siot_channels_supported()) ||
        ((routes & SIOT_ROUTE_GROUP) && !label_valid(group))) return -RT_EINVAL;
    /* Each label is <=24 bytes; every format below fits in 64 bytes. */
    snprintf(c->prefix, sizeof(c->prefix), "%s_dev_%s", project, device);
    snprintf(c->state, sizeof(c->state), "%s_dev_%s/state", project, device);
    snprintf(c->ack, sizeof(c->ack), "%s_dev_%s/ack", project, device);
    if (routes & SIOT_ROUTE_DEVICE)
        snprintf(c->device, sizeof(c->device), "%s_dev_%s/cmd", project, device);
    if (routes & SIOT_ROUTE_GROUP)
        snprintf(c->group, sizeof(c->group), "%s_group_%s/cmd", project, group);
    if (routes & SIOT_ROUTE_BROADCAST)
        snprintf(c->broadcast, sizeof(c->broadcast), "%s_all/cmd", project);
    c->routes = routes;
    return 0;
}
int siot_channels_init_from_config(struct siot_channels *channels)
{
    const char *group = RT_NULL;
#ifdef SIOT_CHANNEL_GROUP
    group = SIOT_CHANNEL_GROUP_ID;
#endif
    return siot_channels_init(channels, SIOT_CHANNEL_PROJECT,
                             SIOT_CHANNEL_DEVICE_ID, group, siot_channels_supported());
}
int siot_channels_subscribe(struct siot_client *client, const struct siot_channels *c)
{
    const char *topics[3];
    unsigned int masks[3] = {SIOT_ROUTE_DEVICE, SIOT_ROUTE_GROUP, SIOT_ROUTE_BROADCAST};
    int i, result;
    if (!client || !c || !c->prefix[0] || (c->routes & ~siot_channels_supported())) return -RT_EINVAL;
    topics[0] = c->device; topics[1] = c->group; topics[2] = c->broadcast;
    for (i = 0; i < 3; ++i)
        if (c->routes & masks[i])
        {
            result = siot_client_subscribe(client, topics[i]);
            if (result) return result;
        }
    return 0;
}
int siot_channels_match(const struct siot_channels *c, const char *topic, int length, int retained)
{
    const char *topics[3];
    unsigned int masks[3] = {SIOT_ROUTE_DEVICE, SIOT_ROUTE_GROUP, SIOT_ROUTE_BROADCAST};
    int i;
    if (!c || !c->prefix[0] || !topic || length <= 0 || length >= SIOT_CHANNEL_TOPIC_SIZE ||
        (c->routes & ~siot_channels_supported())) return -RT_EINVAL;
    topics[0] = c->device; topics[1] = c->group; topics[2] = c->broadcast;
    for (i = 0; i < 3; ++i)
        if ((c->routes & masks[i]) && (int)strlen(topics[i]) == length && !memcmp(topic, topics[i], length))
        {
#ifdef SIOT_CHANNEL_REJECT_RETAINED
            if (retained) return -RT_EINVAL;
#else
            (void)retained;
#endif
            return (int)masks[i];
        }
    return SIOT_ROUTE_NONE;
}
#endif
