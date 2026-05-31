/*
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the debugprobe project.
 * See the repository LICENSE file for full terms.
 */

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"

#include "tusb.h"

#include "cdc_perf.h"
#include "i2c_bridge.h"
#include "probe_config.h"

TaskHandle_t i2c_taskhandle;

static TickType_t last_wake;
static TickType_t interval = 20;

static uint8_t echo_buf[CFG_TUD_CDC_EP_BUFSIZE];

static inline uint8_t toggle_ascii_case(uint8_t c) {
    if ((c >= 'a') && (c <= 'z')) {
        return (uint8_t)(c - ('a' - 'A'));
    }
    if ((c >= 'A') && (c <= 'Z')) {
        return (uint8_t)(c + ('a' - 'A'));
    }
    return c;
}

void i2c_bridge_init(void) {
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

#ifdef CDC_I2C_HW_TRIGGER
    gpio_init(CDC_I2C_HW_TRIGGER);
    gpio_set_dir(CDC_I2C_HW_TRIGGER, GPIO_OUT);
    gpio_put(CDC_I2C_HW_TRIGGER, 0);
#endif
}

bool i2c_bridge_task(void) {
    bool keep_alive = false;
    bool wrote_data = false;

#ifdef CDC_I2C_HW_TRIGGER
    gpio_put(CDC_I2C_HW_TRIGGER, 1);
#endif

    while (tud_cdc_n_available(CDC_I2C) > 0) {
        size_t readable = tud_cdc_n_available(CDC_I2C);
        size_t writable = tud_cdc_n_write_available(CDC_I2C);
        size_t len;

        if (writable == 0) {
            keep_alive = true;
            break;
        }

        len = MIN(sizeof(echo_buf), readable);
        len = MIN(len, writable);
        if (len == 0) {
            break;
        }

        len = tud_cdc_n_read(CDC_I2C, echo_buf, len);
        if (len == 0) {
            break;
        }

        cdc_perf_record_rx(CDC_PERF_PORT_I2C, len);

        for (size_t i = 0; i < len; ++i) {
            echo_buf[i] = toggle_ascii_case(echo_buf[i]);
        }

        (void)tud_cdc_n_write(CDC_I2C, echo_buf, len);
        cdc_perf_record_tx(CDC_PERF_PORT_I2C, len);
        wrote_data = true;
    }

    if (wrote_data) {
        tud_cdc_n_write_flush(CDC_I2C);
    }

#ifdef CDC_I2C_HW_TRIGGER
    gpio_put(CDC_I2C_HW_TRIGGER, 0);
#endif

    return keep_alive;
}

void i2c_bridge_thread(void *ptr) {
    (void)ptr;

    last_wake = xTaskGetTickCount();

    while (1) {
        bool keep_alive = i2c_bridge_task();

        if (!keep_alive) {
            BaseType_t delayed = xTaskDelayUntil(&last_wake, interval);
            if (delayed == pdFALSE) {
                last_wake = xTaskGetTickCount();
            }
        }
    }
}
