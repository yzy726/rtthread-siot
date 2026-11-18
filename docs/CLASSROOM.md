# 多设备部署与老师控制端（v1.1.0）

部署者分别选择固件支持的功能，以及老师服务开放的功能。原版 SIoT Linux
服务继续使用，老师控制端是独立 Python 程序；无需修改 SIoT 服务本体。

## 1. 固件的可选项

在 RT-Thread Settings / menuconfig 的 SIoT 软件包中选择：

| 选项 | 默认 | 作用 |
|---|---|---|
| `SIOT_USING_CHANNELS` | 关闭 | 启用多设备路由；关闭时保留原有 API 和示例 Topic |
| `SIOT_CHANNEL_PROJECT` | `demo` | 这次部署的项目/班级标识 |
| `SIOT_CHANNEL_DEVICE_ID` | `001` | 本项目内每块板唯一的编号 |
| `SIOT_CHANNEL_DEVICE` | 开启 | 允许本板单独控制 |
| `SIOT_CHANNEL_GROUP` | 关闭 | 允许分组控制 |
| `SIOT_CHANNEL_GROUP_ID` | `g01` | 本板所属小组，分组开启后填写 |
| `SIOT_CHANNEL_BROADCAST` | 关闭 | 允许整个项目广播控制 |
| `SIOT_CHANNEL_REJECT_RETAINED` | 开启 | 拒绝带 retained 标记的匹配控制消息 |

项目、设备、小组标识均为 1～24 个 ASCII 字符：首字符为字母或数字，其后可用
字母、数字、连字符 `-`。标识内不含下划线、`/`、`+`、`#`；生成的 Topic 使用
下划线作为分隔，仍只有“项目/设备”两层，兼容 SIoT 1.x 的显示方式。

例如项目 `lab05`、设备 `001`、小组 `g01`：

| 用途 | Topic |
|---|---|
| 本板状态 | `lab05_dev_001/state` |
| 本板回执 | `lab05_dev_001/ack` |
| 单独控制 | `lab05_dev_001/cmd` |
| 小组控制 | `lab05_group_g01/cmd` |
| 全班广播 | `lab05_all/cmd` |

设备 002 使用自己的 `lab05_dev_002/...`，广播/分组收到的命令也从各自 ack
回复。关闭所有控制范围可保留纯数据上报模式。每次重连需重新订阅已启用的范围。

编号相同仍会产生身份和数据冲突。启用通用示例时，其 Client ID 自动采用生成的
设备前缀，因此每块板仍需填写唯一编号；软件包不读取任何特定芯片的 UID。

## 2. 应用如何接入

启用 `SIOT_USING_CHANNELS` 后包含 `siot_channels.h`。创建持久的
`struct siot_channels`，调用 `siot_channels_init_from_config()`，或用
`siot_channels_init()` 传入运行时标识和控制范围掩码。运行时掩码只能选择编译时
已启用范围的子集，不能打开被编译开关关闭的功能。

连接后调用 `siot_channels_subscribe()`；收包回调中调用
`siot_channels_match()`。正数表示 DEVICE/GROUP/BROADCAST，0 表示其他主题，
负值表示输入或保留消息策略拒绝。回调中校验业务消息并投递应用队列，由业务线程
执行后将结果发布至 `channels.ack`。不要在 MQTT 回调里递归调用发布/订阅。

路由层不解析 LED JSON、不驱动硬件，也不自动声称命令已执行。通用 shell 示例
只打印收到的命令并发送 hello 消息，故不会产生“执行成功”回执。课程工程才提供
传感器、LED、蜂鸣器和业务回执。

## 3. 部署者配置老师控制端

需要 Python 3.10+、`paho-mqtt` 2.x；这是电脑/Linux 上的依赖，不是额外的
RT-Thread 固件软件包。安装：

```text
python -m pip install -r tools/teacher/requirements.txt
```

将 `tools/teacher/deployment.example.json` 复制为同目录 `deployment.json`。
编辑服务器、项目名、设备名单和功能开关。例如：

```json
{"features": {
    "individual": true,
    "group": true,
    "broadcast": true,
    "monitor": true
}}
```

这只是 JSON 中的一段，需要放在完整配置文件里。`false` 关闭对应功能；关闭的
控制范围既不会在页面出现，也会被服务端 API 拒绝。`monitor=false` 时不订阅
设备状态，仍接收命令 ack。只读监测可将三个控制范围都设为 false。

配置中的 `actions` 是允许发送的动作列表。示例给出课程用的红灯、蓝灯、蜂鸣器、
关闭输出四个动作。部署者可以替换为其设备应用能理解的 `params`，也可以删去
不开放的动作。网页请求只能选择名单中的目标和动作，不能自定义任意 MQTT Topic。

每个设备填写 `id`、显示名称 `name`、所属 `group`。固件与老师端使用相同的
项目/设备/小组编号及控制范围；控制端开启某范围不会让未订阅它的板自动接收。

检查配置、输出 001 号板的参考配置片段：

```text
python tools/teacher/control.py --config tools/teacher/deployment.json --check-config
python tools/teacher/control.py --config tools/teacher/deployment.json --device-config 001
```

输出不含账号密码。按片段在 Studio 菜单中填写各板配置，不要用片段覆盖整个 BSP
的 `.config`。每位同学编译、烧录自己的配置；更改固件支持范围后需重新烧录。

## 4. 启动老师服务并使用页面

```text
python tools/teacher/control.py --config tools/teacher/deployment.json
```

交互启动时，按提示输入 Web 控制口令和 MQTT 密码；输入不回显。Web 口令至少
16 个无空格的可打印 ASCII 字符，应与 MQTT 密码不同。也可分别通过配置中的
`web.token_env`、`broker.password_env` 指定的环境变量提供，适合服务管理器启动。
实际秘密不写入示例配置、不进入浏览器的配置响应。

默认页面为 `http://127.0.0.1:8765`。输入同一个 Web 控制口令后，可选单台、小组
或全班，再选择动作发送。页面分别显示已执行、设备拒绝和待确认的编号。MQTT
PUBACK 不算设备执行成功；代码为 200 的逐板业务 ack 才计为执行成功。

页面的“最近消息”仅代表最近收到上报/回执的时间，不保证设备此刻仍在线。
离线设备可能错过非保留消息。发送失败可能发生在指令已经到达之后，先检查回执，
不要因 HTTP 或 PUBACK 超时自动生成新 ID 连续重发。控制记录最多保留最近 64 条，
服务重启会清空，不是持久审计库。

部署在 Linux 服务器时，可保持回环监听并通过 SSH 转发访问，例如将本地 8765
转发到服务器 127.0.0.1:8765。需要多台电脑/手机访问时，由部署者配置受控监听、
访问网络及 HTTPS 反向代理；默认的 Python HTTP 服务没有内建 HTTPS，也不代替
生产级反向代理。修改 `deployment.json` 后重启老师服务以应用选择。

使用 MQTT TLS 时将 `broker.tls` 设为 true，并配置实际 TLS 端口及 `ca_file`。
CA 路径相对部署配置文件；空路径使用系统信任 CA。主机名/证书校验保持开启。

## 5. 可选的 Broker 授权规则

功能开关限制的是这套控制端和固件的正常使用路径。SIoT v1 共享 MQTT 账号本身
没有因此变成教师/学生分权。知道同一 MQTT 密码的其他客户端，仍可能绕过页面
直接向 Topic 发布；固件路由也不是加密鉴权。

若部署者需要教师专属控制，可导出与所选功能一致的 **Mosquitto 2.x ACL 示例**：

```text
python tools/teacher/control.py --config tools/teacher/deployment.json --acl teacher
```

输出中教师账号为 `teacher`，设备账号为 `device-001` 等。教师仅可向已启用的
控制 Topic 发布，设备仅可写自己的数据/ack，并读已允许的控制 Topic。部署者需
另行创建独立账号/密码，将规则装入实际支持它的 Broker，禁止匿名，并验证允许与
拒绝两种操作。该命令仅输出规则，不会安装、重载或更改服务器。

**原版 SIoT v1 不识别 Mosquitto 的 ACL 文件。** 若采用独立受保护 Broker，同时
保留 SIoT 展示数据，可设计从受保护 Broker 到 SIoT 的单向数据/ack 桥接。板和
老师控制端都连接受保护入口，控制不从共享账号入口回流；还需限制原共享入口的
网络访问。桥接、账号和 Broker 安装由部署者按其服务环境配置，本包不自动修改。

参考：[SIoT v1 安全设置](https://siot.readthedocs.io/zh-cn/latest/6.advanced/01_safe.html)、
[Mosquitto ACL 文档](https://mosquitto.org/man/mosquitto-conf-5.html)、
[Paho Python 官方文档](https://eclipse.dev/paho/files/paho.mqtt.python/html/)。

## 6. 验证边界

软件包包含真实 C 路由的 16 组合测试、控制端策略/HTTP 鉴权测试、真实 Paho 与
本地 MQTT 测试 Broker 的广播、逐板回执、拒绝订阅和重连测试。这些不代表已在
全班实板或你的公网服务部署。先用 2～3 块实板验证独立数据、范围开关、单控、
小组、广播和每块板自己的回执，再逐步增加数量。
