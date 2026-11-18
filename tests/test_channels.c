/* SPDX-License-Identifier: Apache-2.0 */
#include "siot_channels.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int calls, fail_at;
static const char *wanted[3];
int siot_client_subscribe(struct siot_client *client, const char *topic)
{
    assert(client != NULL);
    assert(!strcmp(topic, wanted[calls++]));
    return fail_at && calls == fail_at ? -23 : 0;
}
int main(void)
{
    struct siot_channels c;
    struct siot_client client;
    unsigned int mask;
    int count, i;
    char maximum[25], overlong[26];
    const char *invalid[] = {"", "a/b", "a+", "a#", "with_space", "-first", "with space", "\xc3\xa9"};
    const char *topics[] = {"demo_dev_001/cmd", "demo_group_g01/cmd", "demo_all/cmd"};
    unsigned int bits[] = {1, 2, 4};
    assert(siot_channels_supported() == EXPECT_ROUTES);
    for (mask = 0; mask < 16; ++mask)
    {
        int result = siot_channels_init(&c, "demo", "001", "g01", mask);
        if (mask & ~EXPECT_ROUTES) { assert(result < 0 && !c.prefix[0]); continue; }
        assert(!result);
        assert(!strcmp(c.prefix, "demo_dev_001"));
        assert(!strcmp(c.state, "demo_dev_001/state"));
        assert(!strcmp(c.ack, "demo_dev_001/ack"));
        for (i = 0; i < 3; ++i)
        {
            int matched = siot_channels_match(&c, topics[i], (int)strlen(topics[i]), 0);
            assert(matched == ((mask & bits[i]) ? (int)bits[i] : 0));
            matched = siot_channels_match(&c, topics[i], (int)strlen(topics[i]), 1);
            assert(matched == ((mask & bits[i]) ? (EXPECT_REJECT_RETAINED ? -RT_EINVAL : (int)bits[i]) : 0));
        }
        assert(siot_channels_match(&c, "demo_dev_001/cmd/x", 18, 0) == 0);
        assert(siot_channels_match(&c, "demo_dev_002/cmd", 16, 0) == 0);
        assert(siot_channels_match(&c, NULL, 1, 0) < 0);
        assert(siot_channels_match(&c, topics[0], -1, 0) < 0);
        assert(siot_channels_match(&c, topics[0], 64, 0) < 0);
        count = 0;
        for (i = 0; i < 3; ++i) if (mask & bits[i]) wanted[count++] = topics[i];
        calls = fail_at = 0;
        assert(!siot_channels_subscribe(&client, &c) && calls == count);
        calls = 0;
        assert(!siot_channels_subscribe(&client, &c) && calls == count); /* reconnect */
        for (i = 1; i <= count; ++i)
        {
            calls = 0; fail_at = i;
            assert(siot_channels_subscribe(&client, &c) == -23 && calls == i);
        }
    }
    for (i = 0; i < (int)(sizeof(invalid)/sizeof(invalid[0])); ++i)
    {
        assert(siot_channels_init(&c, invalid[i], "001", "g01", 0) < 0);
        assert(siot_channels_init(&c, "demo", invalid[i], "g01", 0) < 0);
        if (EXPECT_ROUTES & 2) assert(siot_channels_init(&c, "demo", "001", invalid[i], 2) < 0);
    }
    memset(maximum, 'a', 24); maximum[24] = 0;
    memset(overlong, 'a', 25); overlong[25] = 0;
    assert(!siot_channels_init(&c, maximum, maximum, maximum, EXPECT_ROUTES));
    assert(strlen(c.state) < SIOT_CHANNEL_TOPIC_SIZE && strlen(c.ack) < SIOT_CHANNEL_TOPIC_SIZE);
    assert(siot_channels_init(&c, overlong, "001", "g01", 0) < 0);
    assert(siot_channels_init(&c, NULL, "001", "g01", 0) < 0);
    assert(siot_channels_init(NULL, "demo", "001", "g01", 0) < 0);
    assert(!siot_channels_init_from_config(&c));
    printf("PASS channel routes=%u reject_retained=%d\n", EXPECT_ROUTES, EXPECT_REJECT_RETAINED);
    return 0;
}
