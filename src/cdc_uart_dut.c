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
#include "cdc_uart_dut.h"
#include "probe_config.h"

TaskHandle_t uart_dut_taskhandle;
static TickType_t last_wake, interval = 20; //100;
static volatile TickType_t break_expiry;
static volatile bool timed_break;

/* Max 1 FIFO worth of data */
static uint8_t tx_buf[CFG_TUD_CDC_EP_BUFSIZE];  //32
static uint8_t rx_buf[CFG_TUD_CDC_EP_BUFSIZE];  //32

// Actually s^-1 so 25ms
#define DEBOUNCE_DUT_MS 40
static uint debounce_ticks = 5;

// static BaudInfo_t baud_info;

void cdc_uart_dut_init(void) {
    gpio_set_function(HW_UART_DUT_TX, GPIO_FUNC_UART);
    gpio_set_function(HW_UART_DUT_RX, GPIO_FUNC_UART);
    gpio_set_pulls(HW_UART_DUT_TX, 1, 0);
    gpio_set_pulls(HW_UART_DUT_RX, 1, 0);
    uart_init(HW_UART_DUT_INTERFACE, HW_UART_DUT_BAUDRATE);

#ifdef  HW_UART_DUT_RX_HW_TRIGGER
    gpio_init( HW_UART_DUT_RX_HW_TRIGGER);
    gpio_set_dir( HW_UART_DUT_RX_HW_TRIGGER, GPIO_OUT);
#endif
#ifdef  HW_UART_DUT_TX_HW_TRIGGER
    gpio_init( HW_UART_DUT_TX_HW_TRIGGER);
    gpio_set_dir( HW_UART_DUT_TX_HW_TRIGGER, GPIO_OUT);
#endif


}

bool cdc_uart_dut_task(void)
{
    static int was_connected = 0;
    static uint cdc_tx_oe = 0;
    uint rx_len = 0;
    bool keep_alive = false;

#ifdef  HW_UART_DUT_RX_HW_TRIGGER
    gpio_put( HW_UART_DUT_RX_HW_TRIGGER, 1);
#endif
    // Consume uart fifo regardless even if not connected
    while(uart_is_readable(HW_UART_DUT_INTERFACE) && (rx_len < sizeof(rx_buf))) {
        rx_buf[rx_len++] = uart_getc(HW_UART_DUT_INTERFACE);
    }

    if (tud_cdc_n_connected(CDC_UART_DUT)) {
        was_connected = 1;
        int written = 0;
        /* Implicit overflow if we don't write all the bytes to the host.
         * Also throw away bytes if we can't write... */
        if (rx_len) {
          uint32_t t_start_us = time_us_32();

          written = MIN(tud_cdc_n_write_available(CDC_UART_DUT), rx_len);
          if (rx_len > written)
              cdc_tx_oe++;

          if (written > 0) {
            tud_cdc_n_write(CDC_UART_DUT, rx_buf, written);
            tud_cdc_n_write_flush(CDC_UART_DUT);
            cdc_perf_record_tx(CDC_PERF_PORT_DUT, (size_t)written);
            cdc_perf_record_latency_us(CDC_PERF_PORT_DUT, time_us_32() - t_start_us);
          }
        } 
#ifdef  HW_UART_DUT_RX_HW_TRIGGER
    gpio_put( HW_UART_DUT_RX_HW_TRIGGER, 0);
#endif

#ifdef  HW_UART_DUT_TX_HW_TRIGGER
      gpio_put( HW_UART_DUT_TX_HW_TRIGGER, 1);
#endif
      /* Reading from a firehose and writing to a FIFO. */
      size_t watermark = MIN(tud_cdc_n_available(CDC_UART_DUT), sizeof(tx_buf));
      if (watermark > 0) {
        uint32_t t_start_us = time_us_32();
        size_t tx_len;

        /* Batch up to half a FIFO of data - don't clog up on RX */
        watermark = MIN(watermark, 16);
        tx_len = tud_cdc_n_read(CDC_UART_DUT, tx_buf, watermark);
        uart_write_blocking(HW_UART_DUT_INTERFACE, tx_buf, tx_len);
        cdc_perf_record_rx(CDC_PERF_PORT_DUT, tx_len);
        cdc_perf_record_latency_us(CDC_PERF_PORT_DUT, time_us_32() - t_start_us);
      }
#ifdef  HW_UART_DUT_TX_HW_TRIGGER
      gpio_put( HW_UART_DUT_TX_HW_TRIGGER, 0);
      gpio_put( HW_UART_DUT_TX_HW_TRIGGER, 1);
#endif

      /* Pending break handling */
      if (timed_break) {
        if (((int)break_expiry - (int)xTaskGetTickCount()) < 0) {
          timed_break = false;
          uart_set_break(HW_UART_DUT_INTERFACE, false);

        } else {
          keep_alive = true;
        }
      }
    } else if (was_connected) {
      tud_cdc_n_write_clear(CDC_UART_DUT);
      uart_set_break(HW_UART_DUT_INTERFACE, false);
      timed_break = false;
      was_connected = 0;

      cdc_tx_oe = 0;
    }
#ifdef  HW_UART_DUT_TX_HW_TRIGGER
      gpio_put( HW_UART_DUT_TX_HW_TRIGGER, 0);
#endif    
    return keep_alive;
}

static void cdc_uart_set_baudrate(uint32_t baudrate) {
  /* Set the tick thread interval to the amount of time it takes to
   * fill up half a FIFO. Millis is too coarse for integer divide.
   */
  uint32_t micros = (1000 * 1000 * 16 * 10) / MAX(baudrate, 1);
  interval = MAX(1, micros / ((1000 * 1000) / configTICK_RATE_HZ));
  debounce_ticks = MAX(1, configTICK_RATE_HZ / (interval * DEBOUNCE_DUT_MS));
  probe_info("New baud rate %ld micros %ld interval %lu\n",
              baudrate, micros, interval);
  uart_deinit(HW_UART_DUT_INTERFACE);
  tud_cdc_n_write_clear(CDC_UART_DUT);
  tud_cdc_n_read_flush(CDC_UART_DUT);

  uart_init(HW_UART_DUT_INTERFACE, baudrate);
}

void cdc_uart_dut_thread(void *ptr)
{
  BaseType_t delayed;
  last_wake = xTaskGetTickCount();
  bool keep_alive;
  /* Threaded with a polling interval that scales according to linerate */
  while (1) {
    keep_alive = cdc_uart_dut_task();
    
    if (!keep_alive) {
      delayed = xTaskDelayUntil(&last_wake, interval);
      if (delayed == pdFALSE)
        last_wake = xTaskGetTickCount();

    }
    
  }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding)
{

  uart_parity_t parity;
  uint data_bits, stop_bits;

  /* Modifying state, so park the thread before changing it. */
  if (tud_cdc_n_connected(CDC_UART_DUT))
    vTaskSuspend(uart_dut_taskhandle);

  cdc_uart_set_baudrate(line_coding->bit_rate);

  switch (line_coding->parity) {
  case CDC_LINE_CODING_PARITY_ODD:
    parity = UART_PARITY_ODD;
    break;
  case CDC_LINE_CODING_PARITY_EVEN:
    parity = UART_PARITY_EVEN;
    break;
  default:
    probe_info("invalid parity setting %u\n", line_coding->parity);
    /* fallthrough */
  case CDC_LINE_CODING_PARITY_NONE:
    parity = UART_PARITY_NONE;
    break;
  }

  switch (line_coding->data_bits) {
  case 5:
  case 6:
  case 7:
  case 8:
    data_bits = line_coding->data_bits;
    break;
  default:
    probe_info("invalid data bits setting: %u\n", line_coding->data_bits);
    data_bits = 8;
    break;
  }

  /* The PL011 only supports 1 or 2 stop bits. 1.5 stop bits is translated to 2,
   * which is safer than the alternative. */
  switch (line_coding->stop_bits) {
  case CDC_LINE_CONDING_STOP_BITS_1_5:
  case CDC_LINE_CONDING_STOP_BITS_2:
    stop_bits = 2;
  break;
  default:
    probe_info("invalid stop bits setting: %u\n", line_coding->stop_bits);
    /* fallthrough */
  case CDC_LINE_CONDING_STOP_BITS_1:
    stop_bits = 1;
  break;
  }

  uart_set_format(HW_UART_DUT_INTERFACE, data_bits, stop_bits, parity);
  /* Windows likes to arbitrarily set/get line coding after dtr/rts changes, so
   * don't resume if we shouldn't */
  if(tud_cdc_n_connected(CDC_UART_DUT))
    vTaskResume(uart_dut_taskhandle);
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{

  /* CDC drivers use linestate as a bodge to activate/deactivate the interface.
   * Resume our UART polling on activate, stop on deactivate */
  if (!dtr) {
  //  vTaskSuspend(uart_dut_taskhandle);   //_AN_02

  } else
    vTaskResume(uart_dut_taskhandle);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t wValue) {
  switch(wValue) {
    case 0:
    uart_set_break(HW_UART_DUT_INTERFACE, false);
    timed_break = false;

    break;
    case 0xffff:
    uart_set_break(HW_UART_DUT_INTERFACE, true);
    timed_break = false;

    break;
    default:
    uart_set_break(HW_UART_DUT_INTERFACE, true);
    timed_break = true;

    break_expiry = xTaskGetTickCount() + (wValue * (configTICK_RATE_HZ / 1000));
    break;
  }
}
