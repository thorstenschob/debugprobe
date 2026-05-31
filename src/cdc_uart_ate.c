/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// serialPort.RtsEnable = true; // Hinweis für alle C# und anderen Programme
// serialPort.DtrEnable = true; // Hinweis für alle C# und anderen Programme

#include "FreeRTOS.h"
#include "task.h"

#include <pico/stdlib.h>

#include "tusb.h"

#include "cdc_perf.h"
#include "cdc_uart_ate.h"
#include "probe_config.h"

TaskHandle_t uart_ate_taskhandle;
static TickType_t last_wake, interval = 20; //100;
static volatile TickType_t break_expiry;
static volatile bool timed_break;

/* Max 1 FIFO worth of data */
static uint8_t tx_buf[CFG_TUD_CDC_EP_BUFSIZE];  //32
static uint8_t rx_buf[CFG_TUD_CDC_EP_BUFSIZE];  //32

// static BaudInfo_t baud_info;

void cdc_uart_ate_init(void) {
    gpio_set_function(HW_UART_ATE_TX, GPIO_FUNC_UART);
    gpio_set_function(HW_UART_ATE_RX, GPIO_FUNC_UART);
    gpio_set_pulls(HW_UART_ATE_TX, 1, 0);
    gpio_set_pulls(HW_UART_ATE_RX, 1, 0);
    uart_init(HW_UART_ATE_INTERFACE, HW_UART_ATE_BAUDRATE);

#ifdef  HW_UART_ATE_RX_HW_TRIGGER
    gpio_init( HW_UART_ATE_RX_HW_TRIGGER);
    gpio_set_dir( HW_UART_ATE_RX_HW_TRIGGER, GPIO_OUT);
#endif
#ifdef  HW_UART_ATE_TX_HW_TRIGGER
    gpio_init( HW_UART_ATE_TX_HW_TRIGGER);
    gpio_set_dir( HW_UART_ATE_TX_HW_TRIGGER, GPIO_OUT);
#endif


}

bool cdc_uart_ate_task(void)
{
    static int was_connected = 0;
    static uint cdc_tx_oe = 0;
    uint rx_len = 0;
    bool keep_alive = false;

#ifdef  HW_UART_ATE_RX_HW_TRIGGER
    gpio_put( HW_UART_ATE_RX_HW_TRIGGER, 1);
#endif
    // Consume uart fifo regardless even if not connected
    while(uart_is_readable(HW_UART_ATE_INTERFACE) && (rx_len < sizeof(rx_buf))) {
        rx_buf[rx_len++] = uart_getc(HW_UART_ATE_INTERFACE);
    }

    if (tud_cdc_n_connected(CDC_UART_ATE)) {
        was_connected = 1;
        int written = 0;
        /* Implicit overflow if we don't write all the bytes to the host.
         * Also throw away bytes if we can't write... */
        if (rx_len) {
          uint32_t t_start_us = time_us_32();

          written = MIN(tud_cdc_n_write_available(CDC_UART_ATE), rx_len);
          if (rx_len > written)
              cdc_tx_oe++;

          if (written > 0) {
            tud_cdc_n_write(CDC_UART_ATE, rx_buf, written);
            tud_cdc_n_write_flush(CDC_UART_ATE);
            cdc_perf_record_tx(CDC_PERF_PORT_ATE, (size_t)written);
            cdc_perf_record_latency_us(CDC_PERF_PORT_ATE, time_us_32() - t_start_us);
          }
        } 
#ifdef  HW_UART_ATE_RX_HW_TRIGGER
    gpio_put( HW_UART_ATE_RX_HW_TRIGGER, 0);
#endif

#ifdef  HW_UART_ATE_TX_HW_TRIGGER
      gpio_put( HW_UART_ATE_TX_HW_TRIGGER, 1);
#endif
      /* Reading from a firehose and writing to a FIFO. */
      size_t watermark = MIN(tud_cdc_n_available(CDC_UART_ATE), sizeof(tx_buf));
      if (watermark > 0) {
        uint32_t t_start_us = time_us_32();
        size_t tx_len;

        /* Batch up to half a FIFO of data - don't clog up on RX */
        watermark = MIN(watermark, 16);
        tx_len = tud_cdc_n_read(CDC_UART_ATE, tx_buf, watermark);
        uart_write_blocking(HW_UART_ATE_INTERFACE, tx_buf, tx_len);
        cdc_perf_record_rx(CDC_PERF_PORT_ATE, tx_len);
        cdc_perf_record_latency_us(CDC_PERF_PORT_ATE, time_us_32() - t_start_us);
      }
#ifdef  HW_UART_ATE_TX_HW_TRIGGER
      gpio_put( HW_UART_ATE_TX_HW_TRIGGER, 0);
      gpio_put( HW_UART_ATE_TX_HW_TRIGGER, 1);
#endif

      /* Pending break handling */
      if (timed_break) {
        if (((int)break_expiry - (int)xTaskGetTickCount()) < 0) {
          timed_break = false;
          uart_set_break(HW_UART_ATE_INTERFACE, false);

        } else {
          keep_alive = true;
        }
      }
    } else if (was_connected) {
      tud_cdc_n_write_clear(CDC_UART_ATE);
      uart_set_break(HW_UART_ATE_INTERFACE, false);
      timed_break = false;
      was_connected = 0;

      cdc_tx_oe = 0;
    }
#ifdef  HW_UART_ATE_TX_HW_TRIGGER
      gpio_put( HW_UART_ATE_TX_HW_TRIGGER, 0);
#endif    
    return keep_alive;
}

void cdc_uart_ate_thread(void *ptr)
{
  BaseType_t delayed;
  last_wake = xTaskGetTickCount();
  bool keep_alive;
  /* Threaded with a polling interval that scales according to linerate */
  while (1) {
    keep_alive = cdc_uart_ate_task();
    
    if (!keep_alive) {
      delayed = xTaskDelayUntil(&last_wake, interval);
      if (delayed == pdFALSE)
        last_wake = xTaskGetTickCount();

    }
    
  }
}

