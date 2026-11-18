/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SIOT_CHANNELS_H
#define SIOT_CHANNELS_H
#include "siot_client.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Optional routing, not MQTT authorization. All names are ASCII labels of
 * 1..24 characters: an alphanumeric first character, then alphanumeric or '-'.
 * The two-level topics remain compatible with SIoT 1.x project/device pages. */
#define SIOT_CHANNEL_TOPIC_SIZE 64
enum siot_route
{
    SIOT_ROUTE_NONE = 0,
    SIOT_ROUTE_DEVICE = 1,
    SIOT_ROUTE_GROUP = 2,
    SIOT_ROUTE_BROADCAST = 4
};
struct siot_channels
{
    unsigned int routes;
    char prefix[SIOT_CHANNEL_TOPIC_SIZE];
    char state[SIOT_CHANNEL_TOPIC_SIZE];
    char ack[SIOT_CHANNEL_TOPIC_SIZE];
    char device[SIOT_CHANNEL_TOPIC_SIZE];
    char group[SIOT_CHANNEL_TOPIC_SIZE];
    char broadcast[SIOT_CHANNEL_TOPIC_SIZE];
};

unsigned int siot_channels_supported(void);
int siot_channels_init(struct siot_channels *channels, const char *project,
                       const char *device, const char *group, unsigned int routes);
int siot_channels_init_from_config(struct siot_channels *channels);
/* Call after EVERY connect/reconnect; failures must be handled by the owner. */
int siot_channels_subscribe(struct siot_client *client, const struct siot_channels *channels);
/* Returns a route, NONE for another topic, or -RT_EINVAL for rejected input or
 * a matched retained command when SIOT_CHANNEL_REJECT_RETAINED is enabled.
 * Does not parse payloads, execute actions or publish any acknowledgement. */
int siot_channels_match(const struct siot_channels *channels,
                        const char *topic, int topic_length, int retained);
#ifdef __cplusplus
}
#endif
#endif
