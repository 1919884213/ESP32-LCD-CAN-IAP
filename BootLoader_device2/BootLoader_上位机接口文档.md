# CAN BootLoader 接口

本工程仅支持 CAN IAP，不包含 USART1 烧录协议或串口升级工具。

## 通讯参数

- CAN1：PA11 RX、PA12 TX
- CAN 2.0A，11 位标准数据帧，DLC = 8
- 当前配置：50 kbit/s
- 应用区：`0x08004000 ~ 0x0800FFFF`，最大 48 KiB

## 运动节点 ID

| 用途 | ID |
| --- | --- |
| 主机到 BootLoader 控制 | `0x600` |
| BootLoader 到主机 ACK | `0x610` |
| 主机到 BootLoader 数据 | `0x620` |

## 升级顺序

1. 应用收到 `0x600` 的 `0x02` 后写入 `BKP_DR1=0x4941` 并复位。
2. BootLoader 收到 `START (0x02)`：Byte1 是会话号，Byte2~5 是镜像长度（小端），Byte6~7 是 CRC16-IBM（初值 `0xFFFF`，多项式 `0xA001`）。
3. 每个 `DATA (0x03)` 帧由块号（Byte0~1）和 6 字节固件数据（Byte2~7）组成。
4. `FINISH (0x04)` 触发 BootLoader 对 Flash 镜像的 CRC16 校验。
5. 校验成功后发送 `ACTIVATE (0x05)`，BootLoader 回复 `DONE`、清除 BKP 魔数并复位到应用。

CAN 烧录工具为 `tools/can_iap_flash.py`。
