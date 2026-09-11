# CAN 总线报文 ID 汇总（v2.0.0）

更新日期：2026-08-27

## 网络参数

- 协议：CAN 2.0A 标准数据帧，11 位 ID，非 RTR。
- DLC：当前业务帧和 IAP 帧均为 8 字节。
- 波特率：50 kbit/s。
  - STM32F103：PCLK1 = 36 MHz，预分频 = 72，单 bit = 1 + 8 + 1 = 10 TQ，因此 `36 MHz / 72 / 10 = 50 kbit/s`。
  - ESP32-S3 TWAI：`brp = 160`，单 bit = 1 + 8 + 1 = 10 TQ；按 80 MHz TWAI 时钟计算同为 `80 MHz / 160 / 10 = 50 kbit/s`。
- 上位机：ESP32-S3 LCD；节点 1 为环境节点，节点 2 为运动节点。

## 固件版本

| 固件 | Git 标签 | 提交号 |
| --- | --- | --- |
| 环境节点 Bootloader | `v2.0.0` | `094fa5a` |
| 环境节点 APP | `v2.0.0` | `aaadadc` |
| 运动节点 Bootloader | `v2.0.0` | `caf4a5c` |
| 运动节点 APP | `v2.0.0` | `6340ad9` |

## 业务报文

方向中的“上行”表示节点发往 ESP32 上位机；“下行”表示上位机发往节点。

| ID | 方向 | 发送节点 | 周期/条件 | Byte0..Byte7 |
| --- | --- | --- | --- | --- |
| `0x080` | 上行 | 环境节点 | 条件触发且去重：DHT11 连续错误 >= 10 或 CAN Bus-Off | `nodeId`、`level`、`code(u16, LE)`、`parameter(u16, LE)`、`latch`、`sequence` |
| `0x100` | 上行 | 运动节点 | 与 `0x101` 交替，每帧约 1 s | `speed(u16, 0.01 km/h)`、`rpm(u16)`、`roll(i16, 0.01 deg)`、`pitch(i16, 0.01 deg)` |
| `0x101` | 上行 | 运动节点 | 与 `0x100` 交替，每帧约 1 s | `ax(i16, mg)`、`ay(i16, mg)`、`az(i16, mg)`、`flags`、`counter` |
| `0x110` | 上行 | 运动节点 | 条件：`tick % 1000 == 0`，设计目标约 1 s | `wheelSpeed(u16)`、`odometer(u32)`、`hallPulse(u16)` |
| `0x200` | 上行 | 环境节点 | 1 s | `temperature(i16, 0.01 C)`、`humidity(u16, 0.01 %RH)`、`lightMv(u16)`、`status`、`counter` |
| `0x201` | 上行 | 环境节点 | 1 s | `dhtError`、`lightError`、`rxError`、`txError`、`nodeStatus`、`swVersion`、`uptimeSec(u16, LE)` |
| `0x701` | 上行 | 运动节点 | 条件：`tick % 1000 == 0`，设计目标约 1 s | `status`、`swVersion`、`rxError`、`txError`、`sensorFault`、`taskState`、`uptimeSec(u16, LE)` |
| `0x703` | 上行 | 环境节点 | 1 s | `status`、`swVersion`、`rxError`、`txError`、`sensorFault`、`taskState`、`uptimeSec(u16, LE)` |

说明：多字节字段均为小端序（LE）。当前运动节点代码中 `0x100` 的速度和转速、`0x110` 的全部字段为占位值 `0`；其余字段按采样结果发送。运动节点 `0x110`/`0x701` 使用绝对系统 tick 取模，不保证每次启动都能命中 `0`，因此不能视为已可靠验证的 1 秒周期。

### 关键状态位

- `0x101.Byte6`：`0x01` 静止、`0x02` 运动、`0x04` IMU 有效、`0x08` 轮速有效、`0x10` 姿态有效。
- `0x200.Byte6`：`0x01` DHT11 有效、`0x02` 光照有效、`0x04` DHT11 超时、`0x08` DHT11 校验错误、`0x10` ADC 无效、`0x20` CAN 发送错误。
- 心跳 `Byte0`：`0` 启动中、`1` 正常、`2` 降级、`3` 故障。

## IAP 升级报文

### 运动节点（节点 2）

| ID | 方向 | 用途 | Byte0..Byte7 |
| --- | --- | --- | --- |
| `0x600` | 下行 | IAP 控制 | `cmd`、`session`、参数区 |
| `0x610` | 上行 | IAP 应答 | `cmd`、`status`、`session`、`block(u16, LE)`、`error(u16, LE)`、保留 |
| `0x620` | 下行 | 固件数据 | `block(u16, LE)`、固件数据 6 字节 |

### 环境节点（节点 1）

| ID | 方向 | 用途 | Byte0..Byte7 |
| --- | --- | --- | --- |
| `0x601` | 下行 | IAP 控制 | `cmd`、`session`、参数区 |
| `0x611` | 上行 | IAP 应答 | `cmd`、`status`、`session`、`block(u16, LE)`、`error(u16, LE)`、保留 |
| `0x621` | 下行 | 固件数据 | `block(u16, LE)`、固件数据 6 字节 |

### IAP 命令与状态

| `cmd` | 含义 |
| --- | --- |
| `0x01` | 查询设备信息 |
| `0x02` | 进入/开始 IAP |
| `0x03` | 固件数据块确认 |
| `0x04` | 结束并校验 |
| `0x05` | 激活新 APP 并复位 |
| `0x06` | 查询升级进度 |
| `0x07` | 取消升级 |

常用 `status`：`0x00` 成功、`0x01` 就绪、`0x02` 接收中、`0x04` 校验通过、`0x05` 完成；错误状态从 `0x80` 开始。

## IAP 流程

1. 上位机向目标节点的控制 ID 发送 `cmd = 0x02`，APP 写入 IAP 标志并复位。
2. Bootloader 启动后，上位机再次通过控制 ID 发送带镜像长度和 CRC16 的 `cmd = 0x02`。
3. 上位机通过对应数据 ID 逐块发送，每帧携带 6 字节固件内容；Bootloader 通过对应 ACK ID 确认。
4. 上位机发送 `cmd = 0x04` 校验，成功后发送 `cmd = 0x05` 激活并复位到新 APP。

APP 起始地址为 `0x08004000`，Bootloader 位于 `0x08000000`。

## 验证范围

本文由 `v2.0.0` 源码静态提取，未执行总线抓包、构建、烧录或实机通信验证。
