#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""通过 python-can 向 STM32F103 CAN BootLoader 更新 .bin 固件。

当前 BootLoader 协议：
  START  控制帧：Byte0=0x02, Byte1=session, Byte2..5=长度(uint32 LE),
                 Byte6..7=CRC16-IBM(uint16 LE)
  DATA   数据帧：Byte0..1=block, Byte2..7=最多 6 字节固件数据
  FINISH 控制帧：Byte0=0x04, Byte1=session
  ACTIVATE 控制帧：Byte0=0x05, Byte1=session

首次进入 BootLoader 时可使用 --enter：脚本先向应用发送一次 START，
应用写 BKP 标志并复位；复位完成后脚本再发送带镜像信息的 START。
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

try:
    import can
except ImportError:
    sys.exit("缺少 python-can，请先安装：python -m pip install python-can")

APP_LIMIT = 48 * 1024
CMD_START = 0x02
CMD_DATA = 0x03
CMD_FINISH = 0x04
CMD_ACTIVATE = 0x05
STATUS_OK = 0x00
STATUS_READY = 0x01
STATUS_RECEIVING = 0x02
STATUS_VERIFY_OK = 0x04
STATUS_DONE = 0x05


def crc16_ibm(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if crc & 1 else (crc >> 1)
    return crc


def frame(bus, can_id: int, payload: bytes) -> None:
    bus.send(can.Message(arbitration_id=can_id, is_extended_id=False,
                         data=payload.ljust(8, b"\x00")))


def wait_ack(bus, ack_id: int, command: int, timeout: float) -> tuple[int, bytes]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(max(0.0, deadline - time.monotonic()))
        if msg is None or msg.is_extended_id or msg.arbitration_id != ack_id:
            continue
        data = bytes(msg.data)
        if len(data) >= 2 and data[0] == command:
            return data[1], data
    raise TimeoutError(f"等待 ACK 超时: id=0x{ack_id:03X}, cmd=0x{command:02X}")


def send_control(bus, ctrl_id: int, ack_id: int, command: int, payload: bytes,
                 timeout: float) -> tuple[int, bytes]:
    frame(bus, ctrl_id, bytes([command]) + payload)
    return wait_ack(bus, ack_id, command, timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description="STM32F103 CAN IAP .bin 烧录")
    parser.add_argument("bin", type=pathlib.Path, help="待发送的 .bin 文件")
    parser.add_argument("--device", choices=("1", "2"), default="2",
                        help="目标节点：1 使用 0x601/0x611/0x621，2 使用 0x600/0x610/0x620")
    parser.add_argument("--interface", default="socketcan",
                        help="python-can 接口，例如 socketcan、pcan、slcan、vector")
    parser.add_argument("--channel", default="can0", help="CAN 通道，例如 can0、PCAN_USBBUS1")
    parser.add_argument("--bitrate", type=int, default=50000, help="CAN 波特率，默认 50000")
    parser.add_argument("--session", type=int, default=1, choices=range(256))
    parser.add_argument("--ack-timeout", type=float, default=1.0)
    parser.add_argument("--reset-wait", type=float, default=1.0,
                        help="--enter 后等待应用复位进入 BootLoader 的秒数")
    parser.add_argument("--enter", action="store_true",
                        help="先发送应用侧 START(0x02) 触发 BKP 标志和复位")
    args = parser.parse_args()

    image = args.bin.read_bytes()
    if not image:
        parser.error("bin 文件为空")
    if len(image) > APP_LIMIT:
        parser.error(f"bin 文件过大：{len(image)} B，应用区上限为 {APP_LIMIT} B")

    if args.device == "1":
        ctrl_id, ack_id, data_id = 0x601, 0x611, 0x621
    else:
        ctrl_id, ack_id, data_id = 0x600, 0x610, 0x620

    bus = can.Bus(interface=args.interface, channel=args.channel,
                  bitrate=args.bitrate)
    try:
        if args.enter:
            # 应用只需要命令码和 session；复位后该帧不会保留镜像参数。
            frame(bus, ctrl_id, bytes([CMD_START, args.session]))
            print("已请求应用进入 BootLoader，等待复位...")
            time.sleep(args.reset_wait)

        crc = crc16_ibm(image)
        start_payload = bytes([args.session]) + len(image).to_bytes(4, "little") + crc.to_bytes(2, "little")
        status, _ = send_control(bus, ctrl_id, ack_id, CMD_START,
                                  start_payload, args.ack_timeout)
        if status != STATUS_READY:
            raise RuntimeError(f"START 失败，状态码 0x{status:02X}")
        print(f"BootLoader 已准备：{len(image)} B，CRC16=0x{crc:04X}")

        for block, offset in enumerate(range(0, len(image), 6)):
            chunk = image[offset:offset + 6]
            payload = block.to_bytes(2, "little") + chunk
            frame(bus, data_id, payload)
            status, _ = wait_ack(bus, ack_id, CMD_DATA, args.ack_timeout)
            if status != STATUS_RECEIVING:
                raise RuntimeError(f"DATA block {block} 失败，状态码 0x{status:02X}")
            if block % 64 == 0 or offset + len(chunk) >= len(image):
                print(f"写入进度: {min(offset + len(chunk), len(image))}/{len(image)} B")

        status, _ = send_control(bus, ctrl_id, ack_id, CMD_FINISH,
                                  bytes([args.session]), args.ack_timeout)
        if status != STATUS_VERIFY_OK:
            raise RuntimeError(f"FINISH 校验失败，状态码 0x{status:02X}")
        print("镜像 CRC 校验通过")

        status, _ = send_control(bus, ctrl_id, ack_id, CMD_ACTIVATE,
                                  bytes([args.session]), args.ack_timeout)
        if status != STATUS_DONE:
            raise RuntimeError(f"ACTIVATE 失败，状态码 0x{status:02X}")
        print("升级完成，设备正在复位运行新固件")
        return 0
    finally:
        bus.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
