# LCD 上位机 v2.0.0

此版本对应两个 STM32F103 节点的 Bootloader 与 APP 固件。升级时，上位机嵌入的节点 APP 二进制必须与下表版本一致。

| 固件 | 工程目录 | Git 标签 | 提交号 | PC13 状态 |
| --- | --- | --- | --- | --- |
| 环境节点 Bootloader | `BootLoader_device1` | `v2.0.0` | `094fa5a` | 闪烁 |
| 环境节点 APP | `CanDevice1` | `v2.0.0` | `aaadadc` | 常亮 |
| 运动节点 Bootloader | `BootLoader_device2` | `v2.0.0` | `caf4a5c` | 闪烁 |
| 运动节点 APP | `CanDevice2` | `v2.0.0` | `6340ad9` | 常亮 |

CAN IAP 绑定：环境节点使用 `0x601/0x611/0x621`，运动节点使用 `0x600/0x610/0x620`。APP 应烧录至 `0x08004000`，Bootloader 位于 `0x08000000`。

#