#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "tusb.h"

#include "cdc_perf.h"
#include "probe_config.h"

#if CDC_PERF_ENABLE

typedef struct {
    uint32_t rx_bytes_total;
    uint32_t tx_bytes_total;
    uint32_t rx_bytes_prev;
    uint32_t tx_bytes_prev;
    uint32_t latency_sum_us;
    uint32_t latency_samples;
    uint32_t latency_max_us;
} cdc_perf_stats_t;

static cdc_perf_stats_t g_stats[CDC_PERF_PORT_COUNT];
static uint32_t g_last_dump_ms;

static void cdc_perf_write_line(const char *line)
{
    size_t len;

    if (!tud_ready() || !tud_cdc_n_connected(CDC_PERF_CDC_ITF)) {
        return;
    }

    len = strlen(line);
    if (tud_cdc_n_write_available(CDC_PERF_CDC_ITF) < len) {
        return;
    }

    tud_cdc_n_write(CDC_PERF_CDC_ITF, line, len);
}

static const char *cdc_perf_port_name(cdc_perf_port_t port)
{
    switch (port) {
    case CDC_PERF_PORT_DUT:
        return "DUT";
    case CDC_PERF_PORT_I2C:
        return "I2C";
    case CDC_PERF_PORT_ATE:
        return "ATE";
    default:
        return "?";
    }
}

void cdc_perf_record_rx(cdc_perf_port_t port, size_t bytes)
{
    if (port >= CDC_PERF_PORT_COUNT) {
        return;
    }
    g_stats[port].rx_bytes_total += (uint32_t)bytes;
}

void cdc_perf_record_tx(cdc_perf_port_t port, size_t bytes)
{
    if (port >= CDC_PERF_PORT_COUNT) {
        return;
    }
    g_stats[port].tx_bytes_total += (uint32_t)bytes;
}

void cdc_perf_record_latency_us(cdc_perf_port_t port, uint32_t latency_us)
{
    if (port >= CDC_PERF_PORT_COUNT) {
        return;
    }

    g_stats[port].latency_sum_us += latency_us;
    g_stats[port].latency_samples += 1;
    if (latency_us > g_stats[port].latency_max_us) {
        g_stats[port].latency_max_us = latency_us;
    }
}

void cdc_perf_periodic_dump(void)
{
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_ms;

    if (g_last_dump_ms == 0) {
        g_last_dump_ms = now_ms;
        return;
    }

    elapsed_ms = now_ms - g_last_dump_ms;
    if (elapsed_ms < CDC_PERF_AUTOPRINT_MS) {
        return;
    }

    for (uint8_t i = 0; i < CDC_PERF_PORT_COUNT; i++) {
        cdc_perf_stats_t *s = &g_stats[i];
        uint32_t rx_delta = s->rx_bytes_total - s->rx_bytes_prev;
        uint32_t tx_delta = s->tx_bytes_total - s->tx_bytes_prev;
        uint32_t rx_bps = (rx_delta * 1000u) / elapsed_ms;
        uint32_t tx_bps = (tx_delta * 1000u) / elapsed_ms;
        uint32_t avg_latency_us = s->latency_samples ? (s->latency_sum_us / s->latency_samples) : 0;
        char line[128];

        snprintf(line,
                 sizeof(line),
                 "[CDC_PERF] %s rx=%luB/s tx=%luB/s lat_avg=%luus lat_max=%luus samples=%lu\r\n",
                 cdc_perf_port_name((cdc_perf_port_t)i),
                 (unsigned long)rx_bps,
                 (unsigned long)tx_bps,
                 (unsigned long)avg_latency_us,
                 (unsigned long)s->latency_max_us,
                 (unsigned long)s->latency_samples);
        cdc_perf_write_line(line);

        s->rx_bytes_prev = s->rx_bytes_total;
        s->tx_bytes_prev = s->tx_bytes_total;
        s->latency_sum_us = 0;
        s->latency_samples = 0;
        s->latency_max_us = 0;
    }

    if (tud_ready() && tud_cdc_n_connected(CDC_PERF_CDC_ITF)) {
        tud_cdc_n_write_flush(CDC_PERF_CDC_ITF);
    }

    g_last_dump_ms = now_ms;
}

#else

void cdc_perf_record_rx(cdc_perf_port_t port, size_t bytes)
{
    (void)port;
    (void)bytes;
}

void cdc_perf_record_tx(cdc_perf_port_t port, size_t bytes)
{
    (void)port;
    (void)bytes;
}

void cdc_perf_record_latency_us(cdc_perf_port_t port, uint32_t latency_us)
{
    (void)port;
    (void)latency_us;
}

void cdc_perf_periodic_dump(void)
{
}

#endif
