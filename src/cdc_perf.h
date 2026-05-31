/*
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the debugprobe project.
 * See the repository LICENSE file for full terms.
 */

#ifndef CDC_PERF_H_
#define CDC_PERF_H_

#include <stdint.h>
#include <stddef.h>

typedef enum {
    CDC_PERF_PORT_DUT = 0,
    CDC_PERF_PORT_I2C = 1,
    CDC_PERF_PORT_ATE = 2,
    CDC_PERF_PORT_COUNT
} cdc_perf_port_t;

void cdc_perf_record_rx(cdc_perf_port_t port, size_t bytes);
void cdc_perf_record_tx(cdc_perf_port_t port, size_t bytes);
void cdc_perf_record_latency_us(cdc_perf_port_t port, uint32_t latency_us);
void cdc_perf_periodic_dump(void);

#endif
