#ifndef __CAN_PROTOCOL_H__
#define __CAN_PROTOCOL_H__

#include "main.h"
#include <stdint.h>

/*==========================================================================
 * CAN 协议总览 —— CAN 2.0A 标准帧，DLC = 8
 *--------------------------------------------------------------------------
 * +-----------+---------+--------------------------------------------------+
 * | ID        | 方向    | 数据布局                                         |
 * +-----------+---------+--------------------------------------------------+
 * | 0x100     | 上行    | 运动帧：speed(u16) | rpm(u16) | roll(i16) |      |
 * |           |         | pitch(i16)                                       |
 * +-----------+---------+--------------------------------------------------+
 * | 0x101     | 上行    | 加速度帧：ax(i16) | ay(i16) | az(i16) |      |
 * |           |         | flags | counter                                  |
 * +-----------+---------+--------------------------------------------------+
 * | 0x102     | 上行    | 距离帧：distance(i32, mm) | valid | busy | 0 | 0 |
 * +-----------+---------+--------------------------------------------------+
 * | 0x110     | 上行    | 车轮帧：speed(u16) | odometer(u32) |         |
 * |           |         | hallPulse(u16)                                   |
 * +-----------+---------+--------------------------------------------------+
 * | 0x080     | 上行    | 故障帧                                           |
 * +-----------+---------+--------------------------------------------------+
 * | 0x701     | 上行    | 心跳帧：status | swVersion | rxErr | txErr | |
 * |           |         | sensorFault | taskState | uptimeSec(u16)       |
 * +-----------+---------+--------------------------------------------------+
 * | 0x600     | 下行    | IAP 控制帧：cmd | session | 参数(block/...)   |
 * +-----------+---------+--------------------------------------------------+
 * | 0x610     | 上行    | IAP 应答帧：cmd | status | session |          |
 * |           |         | block(u16) | err(u16) | 保留                    |
 * +-----------+---------+--------------------------------------------------+
 * | 0x620     | 下行    | IAP 数据帧（固件分块下载）                       |
 * +-----------+---------+--------------------------------------------------+
 *--------------------------------------------------------------------------
 * 心跳节点状态 (0x701, Byte0) : BOOTING=0 OK=1 DEGRADED=2 FAULT=3
 * 运动状态标志 (0x101, Byte6) : STATIC=0x01 MOVING=0x02 IMU_VALID=0x04
 *                              WHEEL_VALID=0x08 ATT_VALID=0x10
 * 传感器故障位 (0x701, Byte4) : IMU=0x01
 * IAP ACK 状态  (0x610, Byte1) : OK=0x00 .. DONE=0x05,
 *                                LEN_ERR=0x80 BLOCK_CRC=0x81 CRC32_ERR=0x82
 *                                FLASH_ERR=0x83 VERSION_ERR=0x84
 *                                TIMEOUT=0x85 ADDR_ERR=0x86
 *========================================================================*/

/* CAN 2.0A 标准帧 ID */
#define CAN_ID_MOTION     0x100
#define CAN_ID_ACCEL      0x101
#define CAN_ID_DIST       0x102
#define CAN_ID_WHEEL      0x110
#define CAN_ID_FAULT      0x080
#define CAN_ID_HEARTBEAT  0x701
#define CAN_ID_IAP_CTRL   0x600
#define CAN_ID_IAP_ACK    0x610
#define CAN_ID_IAP_DATA   0x620

/*==========================================================================
 * 运动状态标志位表 (0x101 帧字节 6)
 * +-------+-------+-----------------------------------------+
 * | 位    | 宏    | 含义                                     |
 * +-------+-------+-----------------------------------------+
 * | Bit0  | STATIC      | 设备静止（与 MOVING 互斥）        |
 * | Bit1  | MOVING      | 设备运动（与 STATIC 互斥）        |
 * | Bit2  | IMU_VALID   | MPU6050 数据有效                 |
 * | Bit3  | WHEEL_VALID | 轮速传感器数据有效               |
 * | Bit4  | ATT_VALID   | 姿态角(roll/pitch)有效           |
 * +-------+-------+-----------------------------------------+
 * 例：0x04 = 仅 IMU 有效；0x06 = 运动且 IMU 有效
 *========================================================================*/
#define MOTION_FLAG_STATIC      0x01
#define MOTION_FLAG_MOVING      0x02
#define MOTION_FLAG_IMU_VALID   0x04
#define MOTION_FLAG_WHEEL_VALID 0x08
#define MOTION_FLAG_ATT_VALID   0x10

/*==========================================================================
 * 心跳节点状态表 (0x701 帧字节 0)，枚举互斥，非位标志
 * +------+----------+------------------------------+
 * | 值   | 宏       | 含义                         |
 * +------+----------+------------------------------+
 * | 0    | BOOTING  | 启动中                       |
 * | 1    | OK       | 正常                         |
 * | 2    | DEGRADED | 降级（部分传感器失效）       |
 * | 3    | FAULT    | 故障                         |
 * +------+----------+------------------------------+
 *========================================================================*/
#define HEARTBEAT_STATUS_BOOTING  0
#define HEARTBEAT_STATUS_OK       1
#define HEARTBEAT_STATUS_DEGRADED 2
#define HEARTBEAT_STATUS_FAULT    3

/*==========================================================================
 * 心跳传感器故障位表 (0x701 帧字节 4)，可按位组合
 * +-------+----------+------------------------------+
 * | 位    | 宏       | 含义                         |
 * +-------+----------+------------------------------+
 * | Bit0  | IMU      | MPU6050 故障                 |
 * +-------+----------+------------------------------+
 *========================================================================*/
#define SENSOR_FAULT_IMU 0x01

/*==========================================================================
 * IAP 命令字表 (0x600 帧字节 0)
 * +-------+-----------------+--------------------------------------+
 * | 值    | 宏              | 含义                                 |
 * +-------+-----------------+--------------------------------------+
 * | 0x01  | QUERY_INFO      | 查询设备信息                         |
 * | 0x02  | START           | 进入 IAP 模式                         |
 * | 0x03  | SEND_BLOCK      | 发送固件数据块                       |
 * | 0x04  | END_VERIFY      | 结束并校验固件                       |
 * | 0x05  | ACTIVATE_RESET  | 激活新固件并复位（ACTIVATE 同值）    |
 * | 0x06  | QUERY_PROG      | 查询升级进度                         |
 * | 0x07  | CANCEL          | 取消升级                             |
 * +-------+-----------------+--------------------------------------+
 *========================================================================*/
#define IAP_CMD_QUERY_INFO   0x01
#define IAP_CMD_START        0x02
#define IAP_CMD_SEND_BLOCK   0x03
#define IAP_CMD_END_VERIFY   0x04
#define IAP_CMD_ACTIVATE     0x05
#define IAP_CMD_ACTIVATE_RESET 0x05
#define IAP_CMD_QUERY_PROG   0x06
#define IAP_CMD_CANCEL       0x07

/*==========================================================================
 * IAP 应答状态码表 (0x610 帧字节 1)
 * +-------+-----------------+-------------------------------------------+
 * | 值    | 宏              | 含义                                      |
 * +-------+-----------------+-------------------------------------------+
 * | 0x00  | OK              | 成功                                      |
 * | 0x01  | READY          | 已就绪，等待固件                          |
 * | 0x02  | RECEIVING      | 接收中                                    |
 * | 0x03  | WAIT_RETRANS   | 等待重发（块校验失败）                    |
 * | 0x04  | VERIFY_OK      | 校验通过                                  |
 * | 0x05  | DONE           | 升级完成                                  |
 * | 0x80  | LEN_ERR        | 长度错误                                  |
 * | 0x81  | BLOCK_CRC      | 数据块 CRC 错误                           |
 * | 0x82  | CRC32_ERR      | 整包 CRC32 校验失败                       |
 * | 0x83  | FLASH_ERR      | Flash 写入错误                            |
 * | 0x84  | VERSION_ERR    | 版本错误                                  |
 * | 0x85  | TIMEOUT        | 超时                                      |
 * | 0x86  | ADDR_ERR       | 地址错误                                  |
 * +-------+-----------------+-------------------------------------------+
 *========================================================================*/
#define IAP_STATUS_OK            0x00
#define IAP_STATUS_READY         0x01
#define IAP_STATUS_RECEIVING     0x02
#define IAP_STATUS_WAIT_RETRANS  0x03
#define IAP_STATUS_VERIFY_OK     0x04
#define IAP_STATUS_DONE          0x05
#define IAP_STATUS_LEN_ERR       0x80
#define IAP_STATUS_BLOCK_CRC     0x81
#define IAP_STATUS_CRC32_ERR     0x82
#define IAP_STATUS_FLASH_ERR     0x83
#define IAP_STATUS_VERSION_ERR   0x84
#define IAP_STATUS_TIMEOUT       0x85
#define IAP_STATUS_ADDR_ERR      0x86

#define IAP_ACK_DLC 8
#define IAP_CTRL_DLC 8

/* 心跳上报的软件版本 */
#define SW_VERSION 1

/* MPU6050 加速度计 ±2g 量程灵敏度 */
#define ACCEL_LSB_PER_G 16384

/* 打包 16 位无符号整数（小端，低位在前） */
static inline void pack_u16(uint8_t *buf, uint16_t v)
{
  buf[0] = (uint8_t)(v & 0xFF);
  buf[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* 打包 16 位有符号整数（复用 pack_u16 的小端格式） */
static inline void pack_i16(uint8_t *buf, int16_t v)
{
  pack_u16(buf, (uint16_t)v);
}

/* 打包 32 位无符号整数（小端，低位在前） */
static inline void pack_u32(uint8_t *buf, uint32_t v)
{
  buf[0] = (uint8_t)(v & 0xFF);
  buf[1] = (uint8_t)((v >> 8) & 0xFF);
  buf[2] = (uint8_t)((v >> 16) & 0xFF);
  buf[3] = (uint8_t)((v >> 24) & 0xFF);
}
/* 打包 32 位有符号整数（小端，低位在前） */
static inline void pack_il32(uint8_t *buf, int32_t v) {
  pack_u32(buf,(uint32_t)v);
}

/* 解包 16 位无符号整数（小端） */
static inline uint16_t unpack_u16(const uint8_t *buf)
{
  return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

/* 解包 32 位无符号整数（小端） */
static inline uint32_t unpack_u32(const uint8_t *buf)
{
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
       | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/* 打包运动帧 0x100：speed | rpm | roll | pitch */
static inline void pack_motion_frame(uint8_t *d, uint16_t speed, uint16_t rpm,
                                     int16_t roll, int16_t pitch)
{
  pack_u16(&d[0], speed);
  pack_u16(&d[2], rpm);
  pack_i16(&d[4], roll);
  pack_i16(&d[6], pitch);
}

/* 打包加速度帧 0x101：ax | ay | az | flags | counter */
static inline void pack_accel_frame(uint8_t *d, int16_t ax, int16_t ay, int16_t az,
                                    uint8_t flags, uint8_t counter)
{
  pack_i16(&d[0], ax);
  pack_i16(&d[2], ay);
  pack_i16(&d[4], az);
  d[6] = flags;
  d[7] = counter;
}
/* 打包距离帧 0x102 : 最新值 | 是否有效 | 正在等待 | 0x00 | 0x00  */
static inline void pack_dist_frame(uint8_t* d,int32_t dist,uint8_t valid,uint8_t busy){
  pack_il32(&d[0],dist);
  d[4] = valid;
  d[5] = busy;
  d[6] = 0x00;
  d[7] = 0x00;
}
/* 打包车轮帧 0x110：speed | odometer | hallPulse */
static inline void pack_wheel_frame(uint8_t *d, uint16_t speed,
                                    uint32_t odometer, uint16_t hallPulse)
{
  pack_u16(&d[0], speed);
  pack_u32(&d[2], odometer);
  pack_u16(&d[6], hallPulse);
}

/* 打包心跳帧 0x701：status | swVersion | rxErr | txErr | sensorFault | taskState | uptimeSec */
static inline void pack_heartbeat_frame(uint8_t *d, uint8_t status, uint8_t swVersion,
                                        uint8_t rxErr, uint8_t txErr,
                                        uint8_t sensorFault, uint8_t taskState,
                                        uint16_t uptimeSec)
{
  d[0] = status;
  d[1] = swVersion;
  d[2] = rxErr;
  d[3] = txErr;
  d[4] = sensorFault;
  d[5] = taskState;
  pack_u16(&d[6], uptimeSec);
}

/* 构造 IAP 应答帧 0x610：cmd | status | session | block | err */
static inline void build_iap_ack(uint8_t *d, uint8_t cmd, uint8_t status,
                                 uint8_t session, uint16_t block, uint16_t err)
{
  d[0] = cmd;
  d[1] = status;
  d[2] = session;
  pack_u16(&d[3], block);
  pack_u16(&d[5], err);
  d[7] = 0;
}

#endif
