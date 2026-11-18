# RT-Thread SIoT 通信软件包

用于连接使用者自己的 SIoT 或兼容 MQTT 3.1.1 的服务器。服务器 IP/域名、端口、客户端 ID、用户名、密码及 Topic 均由使用者配置，源码中没有固定的个人服务器或账号，也不依赖特定开发板、传感器和 GPIO。

## 使用条件

RT-Thread 4.1.1 或兼容版本，已工作的网络驱动、SAL/lwIP、POSIX socket/select 和堆。软件包包含 Paho 的 MQTTPacket 编解码部分，无需再安装完整 Paho 客户端；请勿同时启用另一份 `pahomqtt`，避免重复符号。

源码和在线索引分别维护。官方索引合并前，可下载 Release 源码，复制到工程 `packages/siot-v1.1.1`，在 BSP Kconfig 中 source 本目录 `Kconfig`，启用 `PKG_USING_SIOT`。使用在线索引时，使用索引提供的菜单，不要再重复 source 本目录菜单。

v1.1.1 增加显式 QoS 0 发布兼容模式：部分 SIoT v1 服务会断开 QoS 1 发布连接。
API 默认仍为 QoS 1，需要兼容时设置 `config.publish_qos0=RT_TRUE`，通用示例
勾选 `SIOT_EXAMPLE_PUBLISH_QOS0`。QoS 0 成功仅表示发出，不伪称收到 PUBACK。

## 可选的多设备与老师控制端

v1.1.0 增加可手动选择的单独控制、分组、全班广播和保留消息策略；默认关闭多设备层，保留原有用法。状态和回执始终按设备区分。附带独立 Python 网页控制端，部署者用 JSON 选择功能、名单与允许动作，并可导出 Broker ACL 示例。完整设置、接口及权限边界见 [多设备部署教程](docs/CLASSROOM.md)。

## 最快验证

启用 `SIOT_USING_EXAMPLE`，填写你自己的以下选项：

| 选项 | 含义 |
|---|---|
| `SIOT_EXAMPLE_HOST` | SIoT 服务器 IP 或域名，默认空 |
| `SIOT_EXAMPLE_PORT` | 实际 MQTT 端口，默认 1883 |
| `SIOT_EXAMPLE_CLIENT_ID` | 每个连接唯一的客户端 ID |
| `SIOT_EXAMPLE_USERNAME` / `SIOT_EXAMPLE_PASSWORD` | 你自己的 MQTT 账号，默认空 |
| `SIOT_EXAMPLE_SUB_TOPIC` | 接收消息的 Topic |
| `SIOT_EXAMPLE_PUB_TOPIC` | 发送示例消息的 Topic |

先让开发板联网，再在 MSH 中运行 `siot_start`。示例每 5 秒发送一次 `hello from RT-Thread` 并打印收到的消息。运行 `siot_stop` 请求退出；重复启动不会叠加线程。停止需要等待当前连接/收发超时结束。

示例只演示 MQTT 通信，温湿度采集、灯控等业务放在使用者自己的应用中。SIoT 1.x 的 Topic 可采用 `myproject/value` 这样的项目/设备命名，具体以你自己的服务器规则和权限为准。

## API 集成

配置 `struct siot_config`，依次调用 `siot_client_init()`、`siot_client_connect()`、`siot_client_subscribe()`、`siot_client_publish()` 和 `siot_client_poll()`，结束时 `siot_client_disconnect()`。配置字符串在连接期间需要保持有效。

一个 client 的所有操作放在同一线程。接收回调运行在调用线程中，Topic/数据用显式长度表示，不保证以 NUL 结尾；回调中只复制/处理消息，不递归调用 MQTT API。默认发布成功表示收到 PUBACK，显式 QoS 0 模式仅表示发出；业务处理结果需检查对应响应。

完整调用示例见 [README](README.md)，接口行为见 [API 说明](docs/API.md)。

## TLS 和适配范围

TLS 默认关闭。使用 TLS 时需提供兼容 Mbed TLS 2.x 的库、目标芯片熵源、正确的系统时间和服务器根 CA；示例 CA 放在 `examples/siot_example_ca.h`。软件包不强制使用软件 RTC，也不附带某块板卡的 RNG 或内存池实现。

当前嵌入式构建参考为 STM32F407 / RT-Thread 4.1.1；已检查 SIoT v1.3 服务的登录与订阅。SIoT 2.x 通过标准 MQTT 3.1.1 接口设计兼容，尚未实际联调。其他板卡需自行验证网络驱动和 TLS 适配。

## 测试与许可证

`python tests/run_tests.py` 使用真实 C 传输代码测试本地 Broker 的正常和异常报文，并独立解析 Kconfig。代码采用 Apache-2.0，第三方 MQTTPacket 保留原 EPL/EDL 许可和版权说明，详见 [NOTICE](NOTICE)。
