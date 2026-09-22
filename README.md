<div align="center">

# ESP32-LCD-CAN-IAP

**基于 ESP32-S3 与 CAN 总线的车载仪表盘远程固件升级系统**

`ESP32-S3 上位机` · `STM32F103C8T6 × 2 节点` · `CAN 2.0A @ 50 kbit/s` · `LVGL 9 / 10 个页面` · `ESP-IDF ≥ 5.1`

[![MCU](https://img.shields.io/badge/MCU-ESP32--S3-orange?style=flat-square)](https://www.espressif.com/en/products/socs/esp32-s3)
[![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-orange?style=flat-square)](https://www.st.com/en/microcontrollers-microprocessors/stm32f103c8.html)
[![CAN](https://img.shields.io/badge/CAN-2.0A%20%40%2050%20kbit%2Fs-blue?style=flat-square)](#can-协议)
[![IAP](https://img.shields.io/badge/IAP-self--designed%20%2B%20CRC-critical?style=flat-square)](#can-协议)
[![GUI](https://img.shields.io/badge/GUI-LVGL%209-blueviolet?style=flat-square)](#功能一览)
[![Toolchain](https://img.shields.io/badge/Toolchain-ESP--IDF%20%2F%20arm--none--eabi-green?style=flat-square)](#开发环境)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20%2F%20TinyRTOS-yellow?style=flat-square)](#功能一览)
[![License](https://img.shields.io/badge/license-learning%20use%20only-lightgrey?style=flat-square)](#license)

</div>

---

一套「上位机 + 双下位机节点」的车载仪表演示系统：ESP32-S3 负责 LVGL 仪表盘界面与网络业务，两块 STM32F103C8T6 分别作为环境节点和运动节点挂在 CAN 总线上，上位机通过自研的 **CAN IAP 协议**对下位机 Bootloader 进行远程固件升级（无需拆机、无需 SWD 烧录）。

下位机 APP 提供两套运行时版本：**标准 FreeRTOS 版**（`CanDevice*`）与**自研 TinyRTOS 版**（`myCanDevice*`，通过 CMSIS-OS2 兼容层无缝替换内核），两者共用同一套 CAN/IAP 协议，上位机可分别升级。

## 系统架构

```
                 ┌──────────────────────────────┐
   WiFi / MQTT   │        ESP32-S3 上位机        │
  ┌──────────────│  LVGL 9 仪表盘 · TWAI(CAN)   │──── OTA (HTTPS)
  │              │  WiFi/MQTT(OneNET) · LLM对话 │
  │              └──────────┬───────────────────┘
  │                         │ CAN 总线 @ 50 kbit/s
  │              ┌──────────┴──────────┐
  │              │                     │
  │     ┌────────▼────────┐   ┌────────▼────────┐
  │     │ STM32F103 节点1  │   │ STM32F103 节点2  │
  │     │   环境节点       │   │   运动节点        │
  │     │ Bootloader+APP  │   │ Bootloader+APP  │
  │     │ DHT11/光照/NTC  │   │ MPU6050/超声波/霍尔│
  │     └─────────────────┘   └─────────────────┘
```

## 目录结构

```
.
├── LCD/                  ESP32-S3 上位机工程（ESP-IDF ≥ 5.1）
│   ├── main/
│   │   ├── Can/          TWAI(CAN) 收发与 IAP 协议栈（上位机侧）
│   │   ├── HTTPS/        WiFi / MQTT(OneNET) / LLM(智谱) / 和风天气
│   │   ├── LCD/          ST7789 显示、GT911 触摸、背光与 LED
│   │   ├── LVGL/         LVGL 9 移植层
│   │   ├── UI/           10 个仪表页面（运动/环境/IMU/WiFi/OTA/IAP/W25/待机…）
│   │   ├── W25Q/         W25Q64 SPI Flash（GIF 帧/字库/固件镜像缓存）
│   │   └── secrets.h     ← 不入库，见 secrets.h.example
│   ├── CanDevice1.bin    环境节点 APP 固件（FreeRTOS 版，IAP 升级用）
│   ├── CanDevice2.bin    运动节点 APP 固件（FreeRTOS 版）
│   ├── myCanDevice1.bin  环境节点 APP 固件（TinyRTOS 版）
│   └── myCanDevice2.bin  运动节点 APP 固件（TinyRTOS 版）
├── BootLoader_device1/   环境节点 Bootloader（STM32F103, CAN IAP 引导）
├── BootLoader_device2/   运动节点 Bootloader
├── CanDevice1/           环境节点 APP — FreeRTOS 版
├── CanDevice2/           运动节点 APP — FreeRTOS 版
├── myCanDevice1/         环境节点 APP — TinyRTOS 版（自写内核）
└── myCanDevice2/         运动节点 APP — TinyRTOS 版（自写内核 + CMSIS-OS2 兼容层）
```

## 功能一览

### 上位机（LCD/）

| 模块 | 说明 |
| --- | --- |
| 仪表盘 UI | LVGL 9，320×240 运动仪表风主题（碳黑 + 橙），10 个功能页面 |
| CAN 通信 | ESP32 TWAI 驱动，50 kbit/s，收发环境/运动节点业务帧 |
| IAP 升级 | 从本地 / W25Q64 / HTTP 拉取节点固件，走 CAN 协议刷入下位机 Bootloader，带 CRC 校验 |
| OTA 升级 | 上位机自身经 HTTPS 固件服务器 OTA（自定义分区表，factory 单分区 3.9 MB） |
| 云端 | OneNET MQTT 物模型属性上报；和风天气实时天气；智谱 AI 流式对话（LLM 页面） |
| WiFi | 软键盘配网、连接状态机、NVS 持久化凭据 |
| 存储 | W25Q64（GIF 待机动画帧、中文字库、OTA/IAP 固件缓存）、FATFS |

### 下位机节点（STM32F103C8T6）

| 模块 | 说明 |
| --- | --- |
| Bootloader | 位于 `0x08000000`，等待 CAN IAP 指令或跳转 APP（`0x08004000`），带 APP 有效标志持久化 |
| 环境节点 | DHT11 温湿度、GL5528 光照、NTC，周期上报 `0x200/0x201`，故障帧 `0x080` |
| 运动节点 | MPU6050 姿态、超声波测距、霍尔轮速，上报 `0x100/0x101/0x110/0x701` |
| CAN IAP | 节点侧升级指令 `0x601/0x611/0x621`（环境）、`0x600/0x610/0x620`（运动） |

### TinyRTOS 版（myCanDevice\*）

下位机内核由自写 [TinyRTOS](https://github.com/1919884213/TinyRTOS) 替代 FreeRTOS：

- `Core/rtos/` — 位图调度器、协程式上下文切换（`switch.s`）、软件定时器、队列、信号量、优先级继承互斥锁
- `Core/rtos/os_compat.c` — **CMSIS-RTOS v2 → TinyRTOS 兼容层**：既有任务代码（CanTask / MPU6050Task / UltrasonicTask / freertos.c）一行不改即可运行
- 与 `CanDevice*`（FreeRTOS 版）功能完全等价，环境节点固件体积从 29.8 KB 降到 19.5 KB（`CanDevice1.bin` vs `myCanDevice1.bin`）

## CAN 协议

完整报文表见 [`myCanDevice2/CAN总线报文ID汇总_v2.0.0 (1).md`](myCanDevice2/CAN%E6%80%BB%E7%BA%BF%E6%8A%A5%E6%96%87ID%E6%B1%87%E6%80%BB_v2.0.0%20(1).md)。要点：

- CAN 2.0A 标准帧，11 位 ID，DLC=8，50 kbit/s
- 多字节字段小端序
- IAP 握手：擦除 → 分块传输（带序号 + CRC）→ 校验 → 激活重启

## 快速开始

### 1. 下位机（每块 STM32 板）

1. 编译烧录 Bootloader：`BootLoader_device1`（或 2）→ 烧到 `0x08000000`
2. 编译 APP（二选一）：
   - FreeRTOS 版：`CanDevice1` / `CanDevice2`（CMake + arm-none-eabi-gcc，或 STM32CubeIDE 导入）
   - TinyRTOS 版：`myCanDevice1` / `myCanDevice2`
3. APP 烧到 `0x08004000`（或直接用上位机 IAP 页面通过 CAN 刷入仓库内附带的 `LCD/*.bin` 固件）

### 2. 上位机（ESP32-S3）

```bash
cd LCD
idf.py set-target esp32s3
idf.py build flash monitor
```

**首次使用必做**：`main/secrets.h` 不入库，克隆后复制 `main/secrets.h.example` 为 `main/secrets.h`，填入：

- WiFi 热点 SSID / 密码（固件内置默认值，UI 网络页也可现场改）
- 智谱 AI API Key（LLM 对话页）
- 和风天气 API Key（天气页）
- OneNET 产品/设备密钥三元组（MQTT 上报；不用可留占位值，连接会失败但不影响其他功能）

> `managed_components/` 由 `idf.py build` 按 `main/idf_component.yml` 自动拉取；`dependencies.lock` 同样忽略、重新生成。

### 3. IAP 演示

上位机 IAP 页面选择节点（0x600/0x601）与固件源（本地 bin / W25Q64 / HTTP），点升级即可看到进度条；Bootloader 校验 CRC 通过后跳转新 APP，PC13 LED 状态可指示当前运行的是 Bootloader（闪烁）还是 APP（常亮）。

## 固件版本对应

| 固件 | 工程目录 | 标签/提交 | APP 起始地址 |
| --- | --- | --- | --- |
| 环境节点 Bootloader | `BootLoader_device1` | `v2.0.0` @ `094fa5a` | — |
| 运动节点 Bootloader | `BootLoader_device2` | `v2.0.0` @ `caf4a5c` | — |
| 环境节点 APP（FreeRTOS） | `CanDevice1` | `v2.0.0` @ `aaadadc` | `0x08004000` |
| 运动节点 APP（FreeRTOS） | `CanDevice2` | `v2.0.0` @ `6340ad9` | `0x08004000` |
| 环境节点 APP（TinyRTOS） | `myCanDevice1` | 2026-09 起 | `0x08004000` |
| 运动节点 APP（TinyRTOS） | `myCanDevice2` | 2026-09 起 | `0x08004000` |

> 注：本仓库为合并快照，各子工程原始提交历史保存在拆分前的独立仓库中。

## 开发环境

- **ESP32-S3**：ESP-IDF ≥ 5.1（推荐 5.2/5.3），LVGL 9.x（managed component）
- **STM32F103**：arm-none-eabi-gcc 12+ 与 CMake（`CMakePresets.json`），或 STM32CubeIDE；`.ioc` 文件可重新生成 CubeMX 工程
- **烧录工具**：`BootLoader_device*/tools/can_iap_flash.py` 可用 CAN 口直接刷 Bootloader 初始镜像

## License

仅用于学习与个人项目展示。
