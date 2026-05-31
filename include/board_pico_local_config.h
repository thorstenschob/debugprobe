/*
 * Local board overlay for Pico2 multi-interface CDC/DAP modes.
 * Keeps board_pico_config.h close to upstream by isolating local pin/function mappings.
 */

#ifndef BOARD_PICO_LOCAL_CONFIG_H_
#define BOARD_PICO_LOCAL_CONFIG_H_

#define PROBE_TUSB_HW_Trigger 16

#define CDC_UART_DUT 0
#define CDC_I2C      1
#define CDC_UART_ATE 2

#define HW_UART_DUT_INTERFACE uart1
#define HW_UART_DUT_TX 4
#define HW_UART_DUT_RX 5
#define HW_UART_DUT_BAUDRATE 115200
#define HW_UART_DUT_TX_HW_TRIGGER 6
#define HW_UART_DUT_RX_HW_TRIGGER 7

#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_FREQ 400000

#define HW_UART_ATE_INTERFACE uart0
#define HW_UART_ATE_TX 12
#define HW_UART_ATE_RX 13
#define HW_UART_ATE_BAUDRATE 115200
#define HW_UART_ATE_TX_HW_TRIGGER 14
#define HW_UART_ATE_RX_HW_TRIGGER 15

#undef PROBE_PRODUCT_STRING
#if USB_DAP_ENABLE
#define PROBE_PRODUCT_STRING "ATE Probe onPico2(CMSIS-DAP)"
#else
#define PROBE_PRODUCT_STRING "ATE Probe on Pico2 (3x CDC)"
#endif

#endif
