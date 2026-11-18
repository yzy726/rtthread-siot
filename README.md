# SIoT MQTT client for RT-Thread

[中文说明](README_zh.md)

A small MQTT 3.1.1 client for connecting RT-Thread applications to SIoT and compatible brokers. The application supplies its own host, port, client ID, credentials and topics. No server account, board pins, sensor driver or Wi-Fi driver is included.

## Features

- RT-Thread SAL/lwIP sockets, with a single caller-owned client and no hidden worker thread.
- QoS 1 publish with matching PUBACK confirmation; QoS 0/1 receive and checked SUBACK.
- Bounded packet buffers, complete-frame reads, connection/acknowledgement timeouts and keepalive.
- Optional Mbed TLS 2.x integration with CA, hostname and certificate-validity verification.
- An optional shell example using configurable connection settings, with start/stop and retry.
- Vendored Eclipse Paho MQTTPacket codec and its original licenses; no submodules or extra Paho client dependency.

RT-Thread 4.1.1 or compatible APIs, heap, SAL/lwIP and POSIX socket/select support are required. Do not enable the separate `pahomqtt` package at the same time: both packages contain the same MQTTPacket codec symbols.

## Install and configure

The package consists of this source repository and a separate index for `RT-Thread/packages`. The index must be reviewed and merged there before it appears in the official online package list.

For a local installation, copy this directory to `packages/siot-v1.0.1`, source its `Kconfig` from the BSP configuration, and enable `PKG_USING_SIOT`. The BSP must traverse package `SConscript` files. When using the online index, use its configuration and do not source a second copy of the package menu.

Enable `SIOT_USING_EXAMPLE` to build the demonstration. Fill in **your** server, MQTT port, username/password, a unique client ID, and publish/subscribe topics. All host/password defaults are empty. First connect your board to its network, then run:

```text
siot_start
siot_stop
```

The example publishes `hello from RT-Thread` every five seconds to `SIOT_EXAMPLE_PUB_TOPIC` and prints received messages. It does not report simulated sensor measurements or operate any GPIO. Repeated `siot_start` calls do not create another worker; `siot_stop` requests a clean worker exit after the current bounded network operation completes.

For SIoT 1.x, use the server's project/device topic convention, for example `myproject/value`. For another deployment, use that deployment's actual topics and access permissions.

## Application API

```c
#include <siot_client.h>

static struct siot_client client; /* Two 2 KiB buffers by default. */

/* Run all operations for this client in ONE application thread. */
static void connect_to_my_server(void)
{
    struct siot_config cfg = {0};
    cfg.host = "192.168.1.10";       /* Replace with your host. */
    cfg.port = 1883;
    cfg.client_id = "my-board-01";  /* Unique for each connection. */
    cfg.username = "my-user";
    cfg.password = "my-password";
    cfg.keepalive = 60;
    siot_client_init(&client);
    if (siot_client_connect(&client, &cfg) == 0)
    {
        if (siot_client_subscribe(&client, "myproject/cmd") == 0)
        {
            while (siot_client_poll(&client, 100) == 0) { }
        }
    }
    siot_client_disconnect(&client);
}
```

Set `cfg.on_message` to a receive callback and use `siot_client_publish()` for binary or text payloads. Callback topic/payload pointers are length-delimited, may not be NUL-terminated, and expire when the callback returns. Copy messages to an application queue when another thread needs them. Do not call MQTT APIs recursively from the callback.

Configuration strings must remain valid throughout the connection. `publish()` returning zero means the broker sent a matching PUBACK; application/cloud business acceptance is a separate concern. See [API and porting notes](docs/API.md).

## TLS

TLS is disabled by default. Enable `SIOT_USING_TLS`, supply a compatible Mbed TLS 2.x package and target entropy source, set accurate system time, and pass the trusted broker root CA in `cfg.ca_pem`. For the example, put the CA PEM in `examples/siot_example_ca.h`. Use the broker's TLS port. No credentials, CA private keys, hardware RNG implementation or board-specific memory placement are provided by this package.

An empty CA, old system clock or failed verification causes connection failure. The package never disables certificate verification. Software versus hardware RTC selection belongs to the target BSP/application.

## Validation and scope

The C transport is exercised against a local adversarial broker for split frames, interleaved inbound messages/PUBACK, refused authentication/subscription, oversized or malformed packets, truncated frames and missing acknowledgements. Run:

```sh
python -m pip install kconfiglib
python tests/run_tests.py
```

The runner uses GCC and Python on Linux; Windows uses MinGW and Winsock. Package configuration is parsed independently of any lab BSP. RT-Thread 4.1.1/STM32F407 integration is the embedded build reference. A SIoT v1.3 service has been checked for login/subscription; SIoT 2.x service integration and other boards have not yet been validated. Protocol compatibility does not replace target hardware testing.

QoS 2, persistent sessions, will messages, WebSockets and a persistent outbox are outside this version's scope. Reconnection and application message deduplication belong to the caller; the optional example demonstrates reconnection.

## License

New adapter/example code: [Apache-2.0](LICENSE). The vendored Eclipse Paho MQTTPacket retains its copyright headers and [EPL-1.0](third_party/PAHO-LICENSE) / [EDL-1.0](third_party/EDL-LICENSE) licensing. See [NOTICE](NOTICE).
