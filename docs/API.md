# API and porting notes

## Ownership and memory

Allocate a persistent `struct siot_client`, call `siot_client_init()` once, and use it from a single thread. Its default TX/RX storage is 2048 bytes each; avoid putting it on a small thread stack. The structure does not create a worker. Host, username/password, client ID and CA strings are borrowed and must remain valid until disconnection.

Call `siot_client_disconnect()` after a failed connection or when the owner exits. Reconnecting the same initialized client closes any old socket/TLS state first. All operations use RT-Thread SAL/lwIP and blocking waits with bounded deadlines, so they are thread-context APIs, not interrupt APIs.

## Connect and subscribe

`siot_client_connect(client, config)` returns zero after an accepted MQTT 3.1.1 CONNACK. Broker refusal codes are returned as -101 through -105. `client->last_error` preserves error detail. Subscribe accepts one filter per call and checks its matching packet ID and grant; a SUBACK 0x80 is an error.

The session is clean. Subscribe again after reconnecting. The caller supplies the reconnect policy and waits between attempts. The optional shell example retries at five-second intervals.

## Publish and receive

`siot_client_publish(client, topic, payload, length)` sends a QoS 1, non-retained message and waits for the matching PUBACK. Binary payloads are supported. Success confirms MQTT broker receipt, not downstream processing. Apply application IDs/idempotence when duplicate commands matter.

`siot_client_poll(client, timeout_ms)` processes one packet and maintains the keepalive. Call it regularly, including while idle. The timeout is capped at 1000 ms. Inbound PUBLISH can also be dispatched while a publish/subscribe acknowledgement is pending.

The callback receives explicit topic/payload lengths and a retain flag. Buffer pointers are valid only during the callback. Copy to an application queue before returning when needed; do not recursively call a client operation from its callback.

## Limits and failure behavior

- MQTT 3.1.1, IPv4, QoS 0/1 receive and QoS 1 transmit.
- Default packet buffers 2048 bytes; configure 1024 to 8192 bytes each.
- Socket connection, frame I/O and acknowledgement waits use a five-second timeout. DNS is additionally bounded by the chosen lwIP resolver configuration.
- TLS handshake has a 20-second deadline. Interrupted/truncated frames or malformed lengths close the connection to preserve framing.
- QoS 2, WebSockets, will messages, persistent sessions and a persistent outbox are not implemented.
- The stop example command is asynchronous: it cannot interrupt an in-progress DNS/network call immediately.

## TLS porting

The optional TLS backend uses Mbed TLS 2.x SSL/X509/CTR-DRBG/entropy APIs. Supply the maintained TLS package/configuration suitable for the board, an actual entropy source, a correct clock and a trusted root CA. Certificate, hostname and date verification remain required. The backend rejects empty CA data and a system clock older than 2025 before handshaking.

The source package intentionally provides no STM32 HAL, RNG driver, software RTC policy or linker memory section. Those belong to the BSP/application. The empty example CA is a placeholder and is not a private key.

## Optional command channels

When `SIOT_USING_CHANNELS` is enabled, include `siot_channels.h`. Initialize a
persistent `struct siot_channels` from Kconfig or explicit runtime labels/routes,
subscribe after every reconnect, and classify callback topics using the helper.
Its mask may only select a subset of the compiled scopes. Device state/ack topics
are always separate from group and broadcast command topics. The helper has no
payload parser, GPIO side effects or automatic success acknowledgements.

The teacher-side Python tool is independent of the embedded runtime. See
[deployment controls](CLASSROOM.md) for its configuration, HTTP authentication,
scope policy, bounded in-memory history and optional ACL export.

## Third-party codec

`third_party/MQTTPacket` is the codec snapshot carried by the RT-Thread Paho package used during development. Its original headers and EPL/EDL license files remain intact. Do not link a second copy of these codec symbols through `pahomqtt` or another bundled MQTT SDK.
