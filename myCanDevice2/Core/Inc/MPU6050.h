#ifndef __MPU6050_H__
#define __MPU6050_H__

#include "main.h"

#define MPU6050_ADDR       0x68

#define MPU6050_WHO_AM_I   0x75
#define MPU6050_SMPLRT_DIV 0x19
#define MPU6050_CONFIG     0x1A
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_TEMP_OUT_H   0x41

typedef struct {
  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;
  int16_t temp;
} MPU6050_Data_t;

uint8_t MPU6050_Init(void);
HAL_StatusTypeDef MPU6050_ReadByte(uint8_t reg, uint8_t *data);
HAL_StatusTypeDef MPU6050_WriteByte(uint8_t reg, uint8_t data);
HAL_StatusTypeDef MPU6050_ReadAccel(int16_t *ax, int16_t *ay, int16_t *az);
HAL_StatusTypeDef MPU6050_ReadGyro(int16_t *gx, int16_t *gy, int16_t *gz);
HAL_StatusTypeDef MPU6050_ReadTemp(int16_t *temp);

#endif
