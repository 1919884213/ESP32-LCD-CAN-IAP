#include "Display.h"
#include "Timer/Timer.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"

#define TAG "LCD"

/* SPI 时钟：40MHz，足够 ILI9341 全屏刷新 */
#define SPI_HOST       SPI2_HOST
#define SPI_FREQ_HZ    (40 * 1000 * 1000)
#define SPI_MAX_TRANS  (32 * 1024)   /* 32KB，ESP32-S3 DMA 单次上限，一次传完一整块 */

spi_device_handle_t s_spi      = NULL;
static uint16_t            s_width    = 240;
static uint16_t            s_height   = 320;
static Display_Dir_t       s_dir      = DISP_DIR_0;
 
static void lcd_write_cmd(uint8_t cmd) {
    gpio_set_level(DC, 0);
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_write_data(uint8_t data) {
    gpio_set_level(DC, 1);
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_write_data16(uint16_t data) {
    uint8_t buf[2] = { data >> 8, data & 0xFF };
    gpio_set_level(DC, 1);
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = buf,
    };
    spi_device_polling_transmit(s_spi, &t);
}

/* ===== SPI 总线与设备初始化 ===== */

static void spi_bus_init(void) {
    spi_bus_config_t buscfg = {
        .mosi_io_num     = SDI,
        .miso_io_num     = -1,
        .sclk_io_num     = SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = SPI_MAX_TRANS,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_FREQ_HZ,
        .mode           = 0,                /* CPOL=0 CPHA=0，ILI9341 模式0/3 都支持 */
        .spics_io_num   = CS,
        .queue_size     = 8,
        .flags          = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &devcfg, &s_spi));
}

/* ===== GPIO（DC / RESET，CS 由 SPI 驱动接管） ===== */

static void gpio_init_all(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << DC) | (1ULL << RESET),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(DC, 1);
    gpio_set_level(RESET, 1);
}

/* ===== 硬件复位 ===== */

static void lcd_reset(void) {
    gpio_set_level(RESET, 1);
    Timer_DelayMs(20);
    gpio_set_level(RESET, 0);
    Timer_DelayMs(20);
    gpio_set_level(RESET, 1);
    Timer_DelayMs(150);
}

/* ===== ILI9341 寄存器初始化序列 ===== */

static void lcd_init_sequence(void) {
    lcd_write_cmd(0x01);                       /* 软件复位 */
    Timer_DelayMs(150);

    lcd_write_cmd(0x11);                       /* Sleep Out */
    Timer_DelayMs(120);

    lcd_write_cmd(0x3A);                       /* 像素格式 16bit/pixel */
    lcd_write_data(0x55);

    lcd_write_cmd(0xC0);                       /* Power Control 1 */
    lcd_write_data(0x15);
    lcd_write_cmd(0xC1);                       /* Power Control 2 */
    lcd_write_data(0x11);

    lcd_write_cmd(0xC5);                       /* VCOM Control 1 */
    lcd_write_data(0x9D);
    lcd_write_data(0x44);

    lcd_write_cmd(0xC7);                       /* VCOM Control 2 */
    lcd_write_data(0xA0);

    lcd_write_cmd(0x36);                       /* Memory Access Control */
    lcd_write_data((uint8_t)s_dir);

    lcd_write_cmd(0xB1);                       /* Frame Rate Control */
    lcd_write_data(0x00);
    lcd_write_data(0x1B);

    lcd_write_cmd(0xB6);                       /* Display Function Control */
    lcd_write_data(0x0A);
    lcd_write_data(0xA2);

    lcd_write_cmd(0xE0);                       /* Positive Gamma */
    lcd_write_data(0x0F); lcd_write_data(0x31); lcd_write_data(0x2B);
    lcd_write_data(0x0C); lcd_write_data(0x0E); lcd_write_data(0x08);
    lcd_write_data(0x4E); lcd_write_data(0xF1); lcd_write_data(0x37);
    lcd_write_data(0x33); lcd_write_data(0x0C); lcd_write_data(0x12);
    lcd_write_data(0x13); lcd_write_data(0x16); lcd_write_data(0x16);

    lcd_write_cmd(0xE1);                       /* Negative Gamma */
    lcd_write_data(0x09); lcd_write_data(0x16); lcd_write_data(0x2D);
    lcd_write_data(0x0D); lcd_write_data(0x13); lcd_write_data(0x15);
    lcd_write_data(0x40); lcd_write_data(0x19); lcd_write_data(0x36);
    lcd_write_data(0x24); lcd_write_data(0x12); lcd_write_data(0x0C);
    lcd_write_data(0x0D); lcd_write_data(0x25); lcd_write_data(0x1F);

    lcd_write_cmd(0x13);                       /* Normal Display Mode ON */
    Timer_DelayMs(10);

    lcd_write_cmd(0x29);                       /* Display ON */
    Timer_DelayMs(120);
}

/* ===== 对外接口 ===== */

void Display_Init(void) {
    Timer_Init();
    gpio_init_all();
    spi_bus_init();
    lcd_reset();
    lcd_init_sequence();
    Display_SetDirection(s_dir);
    ESP_LOGI(TAG, "ILI9341 init done, %ux%u, dir=0x%02X", s_width, s_height, s_dir);
}

void Display_SetDirection(Display_Dir_t dir) {
    s_dir = dir;
    lcd_write_cmd(0x36);
    lcd_write_data((uint8_t)dir);

    /* 横屏（MV=1）：宽 320 高 240；竖屏：宽 240 高 320 */
    if (dir == DISP_DIR_90 || dir == DISP_DIR_270) {
        s_width  = 320;
        s_height = 240;
    } else {
        s_width  = 240;
        s_height = 320;
    }
}

uint16_t Display_GetWidth(void)  { return s_width; }
uint16_t Display_GetHeight(void) { return s_height; }

void Display_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (x0 > x1) { uint16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { uint16_t t = y0; y0 = y1; y1 = t; }
    if (x1 >= s_width)  x1 = s_width  - 1;
    if (y1 >= s_height) y1 = s_height - 1;

    lcd_write_cmd(0x2A);                       /* 列地址 */
    lcd_write_data16(x0);
    lcd_write_data16(x1);
    lcd_write_cmd(0x2B);                       /* 行地址 */
    lcd_write_data16(y0);
    lcd_write_data16(y1);
    lcd_write_cmd(0x2C);                       /* 写显存 */
}
