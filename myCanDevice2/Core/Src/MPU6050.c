#include "MPU6050.h"
#include "i2c.h"

static HAL_StatusTypeDef MPU6050_ReadBytes(uint8_t reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(&hi2c2, (MPU6050_ADDR << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef MPU6050_WriteBytes(uint8_t reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Write(&hi2c2, (MPU6050_ADDR << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, HAL_MAX_DELAY);
}

HAL_StatusTypeDef MPU6050_ReadByte(uint8_t reg, uint8_t *data)
{
  return MPU6050_ReadBytes(reg, data, 1);
}

HAL_StatusTypeDef MPU6050_WriteByte(uint8_t reg, uint8_t data)
{
  return MPU6050_WriteBytes(reg, &data, 1);
}

uint8_t MPU6050_Init(void)
{
  uint8_t who;

  if (MPU6050_ReadByte(MPU6050_WHO_AM_I, &who) != HAL_OK)
  {
    return 1;
  }
  if ((who & 0x7E) != 0x68)
  {
    return 2;
  }

  if (MPU6050_WriteByte(MPU6050_PWR_MGMT_1, 0x00) != HAL_OK) return 3;
  if (MPU6050_WriteByte(MPU6050_SMPLRT_DIV, 0x07) != HAL_OK) return 4;
  if (MPU6050_WriteByte(MPU6050_CONFIG, 0x03) != HAL_OK) return 5;
  if (MPU6050_WriteByte(MPU6050_GYRO_CONFIG, 0x00) != HAL_OK) return 6;
  if (MPU6050_WriteByte(MPU6050_ACCEL_CONFIG, 0x00) != HAL_OK) return 7;

  return 0;
}

HAL_StatusTypeDef MPU6050_ReadAccel(int16_t *ax, int16_t *ay, int16_t *az)
{
  uint8_t buf[6];
  HAL_StatusTypeDef ret = MPU6050_ReadBytes(MPU6050_ACCEL_XOUT_H, buf, 6);
  if (ret == HAL_OK)
  {
    *ax = (int16_t)((buf[0] << 8) | buf[1]);
    *ay = (int16_t)((buf[2] << 8) | buf[3]);
    *az = (int16_t)((buf[4] << 8) | buf[5]);
  }
  return ret;
}

HAL_StatusTypeDef MPU6050_ReadGyro(int16_t *gx, int16_t *gy, int16_t *gz)
{
  uint8_t buf[6];
  HAL_StatusTypeDef ret = MPU6050_ReadBytes(MPU6050_GYRO_XOUT_H, buf, 6);
  if (ret == HAL_OK)
  {
    *gx = (int16_t)((buf[0] << 8) | buf[1]);
    *gy = (int16_t)((buf[2] << 8) | buf[3]);
    *gz = (int16_t)((buf[4] << 8) | buf[5]);
  }
  return ret;
}

HAL_StatusTypeDef MPU6050_ReadTemp(int16_t *temp)
{
  uint8_t buf[2];
  HAL_StatusTypeDef ret = MPU6050_ReadBytes(MPU6050_TEMP_OUT_H, buf, 2);
  if (ret == HAL_OK)
  {
    *temp = (int16_t)((buf[0] << 8) | buf[1]);
  }
  return ret;
}
